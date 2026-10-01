// Loadable-module network API. This header is intentionally dependency-free
// (no lwip, no internal structures) so kexts can use it with just:
//   -I../../kernel/include
//
// NIC handles are passed as opaque "void *" so a module never needs to see
// the full struct netif internals. All symbols are also exported through the
// module loader's symbol table (see core/util/elf.c).
//
// Copyright (C) 2025 Panagiotis

#ifndef MODULE_NET_H
#define MODULE_NET_H

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

// raw ethernet-frame tap. registered sinks are invoked from handlePacket()
// with the *untouched* incoming frame before it is handed to lwip.
typedef void (*NetPacketSink)(void *nic, void *packet, uint32_t size);
bool netSinkRegister(NetPacketSink sink);
bool netSinkUnregister(NetPacketSink sink);

// first/default NIC (opaque)
void *netDefaultNIC(void);

// copy the default NIC's 6-byte MAC into out[6]; false if no NIC present
bool netDefaultNICMac(uint8_t out[6]);

// build & transmit a full ethernet frame (dst MAC + src MAC + ethertype +
// payload) on the default NIC. `ethertype` is host byte order, e.g.
// NET_ETHERTYPE_IPV4 (0x0800) / NET_ETHERTYPE_IPV6 (0x86DD) / 0x0806.
bool netSendRawMac(uint8_t *destination_mac, uint16_t ethertype, void *data,
                                   uint32_t size);

// Enable IPv6 on all NIC netifs. Returns 0 on success, or a negative errno
// (ERR(ENOTSUP)) when the kernel was built with LWIP_IPV6 disabled.
int netIPv6EnableAll(void);

#ifdef __cplusplus
}
#endif

#endif // MODULE_NET_H