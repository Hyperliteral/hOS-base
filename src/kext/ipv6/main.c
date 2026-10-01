#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>

#include <string.h>

#include <module_net.h>
#include <types.h>

// ipv6 - IPv6 support module for cavOS.
//
// The kernel's network stack (lwip) is normally compiled with LWIP_IPV6=0.
// This module is what *enables* IPv6 once the flag is flipped:
//
//   * module_init calls netIPv6EnableAll() which, on a LWIP_IPV6 build,
//         brings up IPv6 (link-local + autoconfig) on every NIC netif,
//   * on the current (IPv4-only) build it transparently reports the situation,
//   * regardless of the build, it taps the raw ethernet path and monitors
//         IPv6 (ethertype 0x86DD) traffic, deriving RFC 4291 EUI-64 link-local
//         addresses for the default NIC.
//
// Rebuild the kernel with "#define LWIP_IPV6 1" in
// src/kernel/services/net/lwip/include/lwipopts.h to activate the real stack.
// Copyright (C) 2025 Panagiotis

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hyperliteral");
MODULE_DESCRIPTION("IPv6 support (lwip LWIP_IPV6 gate + 0x86DD monitoring)");
MODULE_VERSION("0.1");

#define ETHERTYPE_IPV6_HOST 0x86DD

static uint64_t ipv6FramesSeen = 0;
static uint64_t ipv6BytesSeen = 0;

static void dumpIPv6(void *packet, uint32_t size) {
  uint8_t *b = (uint8_t *)packet;
  if (size < 54)
        return; // eth(14) + ipv6(40) minimum

  pr_info("ipv6: frame %llu bytes=%lu src="
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x dst="
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x next_hdr=0x%02x\n",
                  (unsigned long long)ipv6FramesSeen, (unsigned long)size,
                  b[22], b[23], b[24], b[25], b[26], b[27], b[28], b[29],
                  b[30], b[31], b[32], b[33], b[34], b[35],
                  b[38], b[39], b[40], b[41], b[42], b[43], b[44], b[45],
                  b[46], b[47], b[48], b[49], b[50], b[51], b[6]);
}

static void ipv6FrameSink(void *nic, void *packet, uint32_t size) {
  (void)nic;
  uint8_t *b = (uint8_t *)packet;
  if (size < 14)
        return;
  if ((b[12] << 8 | b[13]) != ETHERTYPE_IPV6_HOST)
        return;

  ipv6FramesSeen++;
  ipv6BytesSeen += size;
  if (ipv6FramesSeen <= 8)
        dumpIPv6(packet, size);
}

// RFC 4291: FE80::/10 from EUI-64, which itself flips the U/L bit of the MAC.
// Local only - never sent on the wire, used purely for reporting.
static void deriveLinkLocal(uint8_t mac[6], uint8_t out[16]) {
  uint8_t eui64[8] = {0};
  eui64[0] = mac[0] ^ 0x02; // U/L bit
  eui64[1] = mac[1];
  eui64[2] = mac[2];
  eui64[3] = 0xFF;
  eui64[4] = 0xFE;
  eui64[5] = mac[3];
  eui64[6] = mac[4];
  eui64[7] = mac[5];

  out[0] = 0xFE;
  out[1] = 0x80;
  out[2] = 0;
  out[3] = 0;
  out[4] = 0;
  out[5] = 0;
  out[6] = 0;
  out[7] = 0;
  memcpy(&out[8], eui64, 8);
}

static int __init ipv6_init(void) {
  if (!netSinkRegister(ipv6FrameSink)) {
        pr_err("ipv6: could not register frame sink!\n");
        return -EBUSY;
  }

  uint8_t mac[6];
  if (netDefaultNICMac(mac)) {
        uint8_t ll[16];
        deriveLinkLocal(mac, ll);
        pr_info("ipv6: default NIC MAC %02x:%02x:%02x:%02x:%02x:%02x, "
                        "EUI-64 link-local "
                        "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                        "%02x%02x:%02x%02x\n",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                        ll[0], ll[1], ll[2], ll[3], ll[4], ll[5], ll[6], ll[7],
                        ll[8], ll[9], ll[10], ll[11], ll[12], ll[13], ll[14], ll[15]);
  } else {
        pr_info("ipv6: no default NIC present (EUI-64 derivation skipped)\n");
  }

  int ret = netIPv6EnableAll();
  if (ret == -ENOTSUP) {
        pr_info("ipv6: kernel built WITHOUT LWIP_IPV6 - monitoring 0x86DD frames "
                        "until lwipopts.h flips the flag\n");
  } else if (ret == 0) {
        pr_info("ipv6: IPv6 enabled on all netifs\n");
  } else {
        pr_warn("ipv6: netIPv6EnableAll returned {%d}\n", ret);
  }

  pr_info("ipv6: loaded\n");
  return 0;
}

static void __exit ipv6_exit(void) {
  netSinkUnregister(ipv6FrameSink);
  pr_info("ipv6: unloaded (saw %llu IPv6 frames, %llu bytes)\n",
                  (unsigned long long)ipv6FramesSeen,
                  (unsigned long long)ipv6BytesSeen);
}

module_init(ipv6_init);
module_exit(ipv6_exit);