#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include <stdatomic.h>
#include <string.h>
#include <system.h>

#include <module_net.h>
#include <socket.h>
#include <types.h>
#include <vfs.h>

// af_packet - a real PF_PACKET (17) packet socket for cavOS, implemented as a
// loadable module over the kernel's raw-frame tap API:
//
//   * module_init registers a NetPacketSink so every incoming ethernet frame
//         (before lwip sees it) fans out into the filtered ring of each open
//         packet socket,
//   * it also registers the AF_PACKET provider, so
//         socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)) returns a working fd
//         (bind / recvfrom / sendto / getsockname via the VFS handler table).
//
// TX is done through netSendRawMac() on the default NIC.
// Copyright (C) 2025 Panagiotis

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hyperliteral");
MODULE_DESCRIPTION("PF_PACKET raw packet socket provider");
MODULE_VERSION("0.1");

#ifndef AF_PACKET
#define AF_PACKET 17
#endif

// musl/sparc use these for socket() flag bits (Linux UAPI values)
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 00004000
#endif

#define ETH_P_ALL_HOST 0x0003
#define ETH_TYPE_ALL 0x0300 // ETH_P_ALL in network byte order, host-stored

#define PACKET_RING_SIZE 16
#define PACKET_FRAME_MAX 1536

// Linux sockaddr_ll (network byte order protocol field!)
typedef struct raw_sockaddr_ll {
  uint16_t sll_family;
  uint16_t sll_protocol;
  int32_t  sll_ifindex;
  uint16_t sll_hatype;
  uint8_t  sll_pkttype;
  uint8_t  sll_halen;
  uint8_t  sll_addr[8];
} raw_sockaddr_ll;

typedef struct PacketFrame {
  bool         used;
  uint16_t len;
  uint8_t  data[PACKET_FRAME_MAX];
} PacketFrame;

typedef struct PacketSock {
  struct PacketSock *next;
  bool                           alive;

  uint16_t filterEthertypeN; // network-order ethertype filter (ETH_TYPE_ALL = all)

  uint32_t   rxHead; // index of oldest unconsumed frame
  uint32_t   rxCount;
  PacketFrame ring[PACKET_RING_SIZE];

  uint64_t rxTotal;
  uint64_t txTotal;
} PacketSock;

static PacketSock  *packetSocks = 0;
static atomic_flag packetSocksLock = ATOMIC_FLAG_INIT;

static void lockPackets(void) {
  while (atomic_flag_test_and_set_explicit(&packetSocksLock,
                                                                                   memory_order_acquire))
        handControl();
}

static void unlockPackets(void) {
  atomic_flag_clear_explicit(&packetSocksLock, memory_order_release);
}

static uint16_t frameEthertypeN(void *packet, uint32_t size) {
  if (size < 14)
        return 0;
  uint8_t *b = (uint8_t *)packet;
  return (uint16_t)((b[12] << 8) | b[13]);
}

// ---------------------------------------------------------------------------
// raw frame fan-out (invoked by the kernel for every received frame)
// ---------------------------------------------------------------------------

static void packetFrameSink(void *nic, void *packet, uint32_t size) {
  (void)nic;

  if (size < 14 || size > PACKET_FRAME_MAX)
        return;

  uint16_t ethertypeN = frameEthertypeN(packet, size);

  lockPackets();
  for (PacketSock *sock = packetSocks; sock; sock = sock->next) {
        if (!sock->alive)
          continue;
        if (sock->filterEthertypeN != ETH_TYPE_ALL &&
                sock->filterEthertypeN != ethertypeN)
          continue;

        if (sock->rxCount >= PACKET_RING_SIZE)
          continue; // drop newest, keep oldest (packet sockets do this)

        PacketFrame *frame = &sock->ring[(sock->rxHead + sock->rxCount) %
                                                                         PACKET_RING_SIZE];
        frame->used = true;
        frame->len = (uint16_t)size;
        memcpy(frame->data, packet, size);
        sock->rxCount++;
        sock->rxTotal++;
  }
  unlockPackets();
}

// ---------------------------------------------------------------------------
// VFS handler table (the fd's "driver")
// ---------------------------------------------------------------------------

static size_t packetSockRecvfrom(OpenFile *fd, uint8_t *out, size_t limit,
                                                                 int flags, sockaddr_linux *addr,
                                                                 uint32_t *len) {
  (void)flags;
  PacketSock *sock = (PacketSock *)fd->dir;
  if (!sock || !sock->alive)
        return -ENOTSOCK;

  lockPackets();
  if (sock->rxCount == 0) {
        unlockPackets();
        return -EAGAIN;
  }

  PacketFrame *frame = &sock->ring[sock->rxHead];
  size_t           copy = frame->len < limit ? frame->len : limit;
  memcpy(out, frame->data, copy);
  sock->rxHead = (sock->rxHead + 1) % PACKET_RING_SIZE;
  sock->rxCount--;
  uint16_t ethertypeN = frameEthertypeN(frame->data, frame->len);

  bool wantAddr = (addr != 0 && len != 0);
  (void)wantAddr;
  if (addr && len) {
        raw_sockaddr_ll *sll = (raw_sockaddr_ll *)addr;
        sll->sll_family = AF_PACKET;
        sll->sll_protocol = ethertypeN;
        sll->sll_ifindex = 0;
        sll->sll_halen = 6;
        sll->sll_addr[0] = frame->data[0];
        sll->sll_addr[1] = frame->data[1];
        sll->sll_addr[2] = frame->data[2];
        sll->sll_addr[3] = frame->data[3];
        sll->sll_addr[4] = frame->data[4];
        sll->sll_addr[5] = frame->data[5];
        *len = sizeof(raw_sockaddr_ll);
  }
  unlockPackets();

  return copy;
}

static size_t packetSockSendto(OpenFile *fd, uint8_t *in, size_t limit,
                                                           int flags, sockaddr_linux *addr, uint32_t len) {
  (void)flags;
  (void)len;
  PacketSock *sock = (PacketSock *)fd->dir;
  if (!sock || !sock->alive)
        return -ENOTSOCK;

  uint8_t dest_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  uint16_t ethertype = 0x0800; // default IPv4 (like Linux when unbound)

  raw_sockaddr_ll *sll = (raw_sockaddr_ll *)addr;
  if (addr && sll->sll_family == AF_PACKET) {
        if (sll->sll_halen == 6)
          memcpy(dest_mac, sll->sll_addr, 6);
        if (sll->sll_protocol)
          ethertype = (uint16_t)((sll->sll_protocol >> 8) | (sll->sll_protocol << 8));
  } else if (sock->filterEthertypeN != ETH_TYPE_ALL) {
        ethertype = (uint16_t)((sock->filterEthertypeN >> 8) |
                                                   (sock->filterEthertypeN << 8));
  }

  if (!netSendRawMac(dest_mac, ethertype, in, (uint32_t)limit))
        return -EIO;

  lockPackets();
  sock->txTotal++;
  unlockPackets();
  return limit;
}

static size_t packetSockBind(OpenFile *fd, sockaddr_linux *addr, size_t len) {
  if (len < sizeof(raw_sockaddr_ll))
        return -EINVAL;

  raw_sockaddr_ll *sll = (raw_sockaddr_ll *)addr;
  if (sll->sll_family != AF_PACKET)
        return -EINVAL;

  PacketSock *sock = (PacketSock *)fd->dir;
  if (!sock || !sock->alive)
        return -ENOTSOCK;

  // sll_protocol is network byte order on Linux; normalize to host-stored
  // network-order bytes so it matches frameEthertypeN()'s output.
  sock->filterEthertypeN = sll->sll_protocol ? sll->sll_protocol : ETH_TYPE_ALL;
  return 0;
}

static size_t packetSockGetsockname(OpenFile *fd, sockaddr_linux *addr,
                                                                        uint32_t *addrlen) {
  PacketSock *sock = (PacketSock *)fd->dir;
  if (!sock || !sock->alive)
        return -ENOTSOCK;
  if (!addr || !addrlen)
        return -EINVAL;

  raw_sockaddr_ll *sll = (raw_sockaddr_ll *)addr;
  sll->sll_family = AF_PACKET;
  sll->sll_protocol = sock->filterEthertypeN;
  sll->sll_ifindex = 0;
  *addrlen = sizeof(raw_sockaddr_ll);
  return 0;
}

static bool packetSockClose(OpenFile *fd) {
  PacketSock *sock = (PacketSock *)fd->dir;
  fd->dir = 0;

  if (!sock)
        return true;

  lockPackets();
  sock->alive = false;

  PacketSock **pp = &packetSocks;
  while (*pp) {
        if (*pp == sock) {
          *pp = sock->next;
          break;
        }
        pp = &(*pp)->next;
  }
  unlockPackets();

  kfree(sock);
  return true;
}

static VfsHandlers packetSocketHandlers = {
        .recvfrom = packetSockRecvfrom,
        .sendto = packetSockSendto,
        .bind = packetSockBind,
        .getsockname = packetSockGetsockname,
        .close = packetSockClose,
};

// ---------------------------------------------------------------------------
// AF_PACKET provider (called from the kernel's socket() syscall)
// ---------------------------------------------------------------------------

static size_t packetSockOpen(void *task, int type, int protocol) {
  bool cloexec = (type & SOCK_CLOEXEC) != 0;
  // nonblock is handled naturally: recvfrom returns -EAGAIN when empty
  type &= ~(SOCK_CLOEXEC | SOCK_NONBLOCK);

  if (type != SOCK_RAW)
        return -ESOCKTNOSUPPORT;

  int socketFd = (int)fsUserOpen(task, "/dev/null", O_RDWR, 0);
  if (socketFd < 0)
        return socketFd;

  OpenFile *fd = fsUserGetNode(task, socketFd);
  if (!fd) {
        fsUserClose(task, socketFd);
        return -EBADF;
  }

  PacketSock *sock = (PacketSock *)kzalloc(sizeof(PacketSock), GFP_KERNEL);
  if (!sock) {
        fsUserClose(task, socketFd);
        return -ENOMEM;
  }

  sock->alive = true;
  sock->filterEthertypeN =
          protocol ? (uint16_t)((protocol >> 8) | (protocol << 8)) : ETH_TYPE_ALL;

  lockPackets();
  sock->next = packetSocks;
  packetSocks = sock;
  unlockPackets();

  fd->handlers = &packetSocketHandlers;
  fd->dir = sock;
  if (cloexec)
        fd->closeOnExec = true;
  // nonblock is handled naturally: recvfrom returns -EAGAIN when empty

  return socketFd;
}

// ---------------------------------------------------------------------------
// module glue
// ---------------------------------------------------------------------------

static int __init afpacket_init(void) {
  if (!netSinkRegister(packetFrameSink)) {
        pr_err("af_packet: could not register frame sink!\n");
        return -EBUSY;
  }

  packetSockProviderRegister((void *)packetSockOpen);
  pr_info("af_packet: loaded (raw frame tap + AF_PACKET socket provider)\n");
  return 0;
}

static void __exit afpacket_exit(void) {
  netSinkUnregister(packetFrameSink);
  packetSockProviderRegister(0);

  lockPackets();
  PacketSock *next;
  for (PacketSock *sock = packetSocks; sock; sock = next) {
        next = sock->next;
        sock->alive = false;
        kfree(sock);
  }
  packetSocks = 0;
  unlockPackets();

  pr_info("af_packet: unloaded\n");
}

module_init(afpacket_init);
module_exit(afpacket_exit);