#include <arpa/inet.h>

// Byte order swaps. x86_64 is little-endian, so host-to-network always swaps;
// kept as functions (not macros) so their addresses can be taken.
uint32_t htonl(uint32_t x) {
  return (x << 24) | ((x << 8) & 0xff0000) | ((x >> 8) & 0xff00) | (x >> 24);
}

uint16_t htons(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }

uint32_t ntohl(uint32_t x) { return htonl(x); }

uint16_t ntohs(uint16_t x) { return htons(x); }
