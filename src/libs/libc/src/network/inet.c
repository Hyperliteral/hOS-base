#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// IPv4/IPv6 address parsing and formatting by hand (no DNS, no libc
// resolver). EAFNOSUPPORT/ENOENT/ENOSPC semantics follow the man pages:
// inet_pton returns 1 ok / 0 bad literal / -1 bad family (errno
// EAFNOSUPPORT); inet_ntop returns NULL with EAFNOSUPPORT or ENOSPC.

// Shared dotted-quad scanner. strict: exactly 4 decimal parts 0-255
// (inet_pton). Non-strict: classic 1-4 part forms a, a.b, a.b.c, a.b.c.d
// with the last part filling the remaining bits, each part decimal/0x
// hex/0-octal via strtoul base 0 (inet_aton/inet_network/inet_addr).
static int scan_v4(const char *cp, uint32_t *out, int strict) {
  if (!cp || !*cp)
    return 0;
  unsigned long parts[4];
  int           n = 0;
  const char   *p = cp;
  for (;;) {
    if (!*p)
      return 0;
    char *end;
    // Strict dotted quads are decimal only; compat forms allow 0x/0 bases.
    unsigned long v = strtoul(p, &end, strict ? 10 : 0);
    if (end == p)
      return 0;
    if (strict && (*p == '+' || *p == '-'))
      return 0;
    if (n >= 4)
      return 0;
    parts[n++] = v;
    p = end;
    if (!*p)
      break;
    if (*p != '.' || n == 4)
      return 0;
    p++;
  }
  uint32_t addr;
  if (strict) {
    if (n != 4)
      return 0;
    for (int i = 0; i < 4; i++)
      if (parts[i] > 255)
        return 0;
    addr = (uint32_t)(parts[0] << 24 | parts[1] << 16 | parts[2] << 8 |
                      parts[3]);
  } else {
    if (n == 1) {
      if (parts[0] > 0xffffffffUL)
        return 0;
      addr = (uint32_t)parts[0];
    } else if (n == 2) {
      if (parts[0] > 255 || parts[1] > 0xffffffUL)
        return 0;
      addr = (uint32_t)(parts[0] << 24 | parts[1]);
    } else if (n == 3) {
      if (parts[0] > 255 || parts[1] > 255 || parts[2] > 0xffffUL)
        return 0;
      addr = (uint32_t)(parts[0] << 24 | parts[1] << 16 | parts[2]);
    } else {
      for (int i = 0; i < 4; i++)
        if (parts[i] > 255)
          return 0;
      addr = (uint32_t)(parts[0] << 24 | parts[1] << 16 | parts[2] << 8 |
                        parts[3]);
    }
  }
  *out = addr; // host byte order; callers apply htonl
  return 1;
}

static int hex_val(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Colon parser with :: compression and embedded dotted-quad tail.
static int scan_v6(const char *s, uint8_t out[16]) {
  uint16_t words[8] = {0};
  // Split head :: tail.
  const char *dbl = strstr(s, "::");
  if (dbl && strstr(dbl + 2, "::"))
    return 0; // two compressions
  int head_n = 0, tail_n = 0;
  // Parse one side into words[8]; returns word count or -1.
  // Embedded IPv4 consumes two words.
  char side[2][64];
  const char *parts[2] = {s, NULL};
  size_t      lens[2] = {dbl ? (size_t)(dbl - s) : strlen(s), 0};
  if (dbl) {
    parts[1] = dbl + 2;
    lens[1] = strlen(parts[1]);
  }
  uint16_t parsed[2][8];
  int      counts[2] = {0, 0};
  for (int side_idx = 0; side_idx < 2; side_idx++) {
    if (!dbl && side_idx == 1)
      break;
    if (lens[side_idx] >= sizeof(side[0]))
      return 0;
    memcpy(side[side_idx], parts[side_idx], lens[side_idx]);
    side[side_idx][lens[side_idx]] = 0;
    char *cur = side[side_idx];
    if (!*cur && !(side_idx == 1 && lens[1] == 0) &&
        !(side_idx == 0 && dbl && lens[0] == 0))
      return 0; // empty non-edge side, e.g. "1::2::" handled above; ":1"
    if (!*cur)
      continue; // leading/trailing "::" edge
    for (;;) {
      char *colon = strchr(cur, ':');
      char  save = 0;
      if (colon) {
        save = *colon;
        *colon = 0;
      }
      if (strchr(cur, '.')) {
        // Embedded dotted quad: must be the last piece.
        if (colon)
          return 0;
        uint32_t v4;
        if (!scan_v4(cur, &v4, 1))
          return 0;
        if (counts[side_idx] > 6)
          return 0;
        parsed[side_idx][counts[side_idx]++] = (uint16_t)(v4 >> 16);
        parsed[side_idx][counts[side_idx]++] = (uint16_t)(v4 & 0xffff);
      } else {
        size_t len = strlen(cur);
        if (len == 0 || len > 4)
          return 0;
        unsigned v = 0;
        for (size_t i = 0; i < len; i++) {
          int h = hex_val(cur[i]);
          if (h < 0)
            return 0;
          v = v * 16 + (unsigned)h;
        }
        if (counts[side_idx] >= 8)
          return 0;
        parsed[side_idx][counts[side_idx]++] = (uint16_t)v;
      }
      if (!colon)
        break;
      *colon = save;
      cur = colon + 1;
    }
  }
  head_n = counts[0];
  tail_n = dbl ? counts[1] : 0;
  if (!dbl) {
    if (head_n != 8)
      return 0;
    for (int i = 0; i < 8; i++)
      words[i] = parsed[0][i];
  } else {
    if (head_n + tail_n > 7)
      return 0; // :: must compress at least one word
    int i = 0;
    for (int k = 0; k < head_n; k++)
      words[i++] = parsed[0][k];
    int zeros = 8 - head_n - tail_n;
    for (int k = 0; k < zeros; k++)
      words[i++] = 0;
    for (int k = 0; k < tail_n; k++)
      words[i++] = parsed[1][k];
  }
  for (int i = 0; i < 8; i++) {
    out[2 * i] = (uint8_t)(words[i] >> 8);
    out[2 * i + 1] = (uint8_t)(words[i] & 0xff);
  }
  return 1;
}

int inet_pton(int af, const char *restrict src, void *restrict dst) {
  if (af == AF_INET) {
    uint32_t v;
    if (!src || !dst)
      return 0;
    if (!scan_v4(src, &v, 1))
      return 0;
    *(uint32_t *)dst = htonl(v);
    return 1;
  }
  if (af == AF_INET6) {
    if (!src || !dst)
      return 0;
    return scan_v6(src, (uint8_t *)dst) ? 1 : 0;
  }
  *__errno_location() = EAFNOSUPPORT;
  return -1;
}

const char *inet_ntop(int af, const void *restrict src, char *restrict dst,
                      socklen_t size) {
  if (af == AF_INET) {
    if (size < INET_ADDRSTRLEN) {
      *__errno_location() = ENOSPC;
      return NULL;
    }
    const uint8_t *b = (const uint8_t *)src;
    snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return dst;
  }
  if (af == AF_INET6) {
    if (size < INET6_ADDRSTRLEN) {
      *__errno_location() = ENOSPC;
      return NULL;
    }
    const uint8_t *b = (const uint8_t *)src;
    uint16_t       w[8];
    for (int i = 0; i < 8; i++)
      w[i] = (uint16_t)((uint16_t)b[2 * i] << 8 | b[2 * i + 1]);
    // Longest zero run of length >= 2 wins; ties go to the first.
    int best_start = -1, best_len = 0;
    for (int i = 0; i < 8;) {
      if (w[i] != 0) {
        i++;
        continue;
      }
      int j = i;
      while (j < 8 && w[j] == 0)
        j++;
      if (j - i > best_len) {
        best_start = i;
        best_len = j - i;
      }
      i = j;
    }
    if (best_len < 2)
      best_start = -1;
    char *p = dst;
    for (int i = 0; i < 8;) {
      if (i == best_start) {
        *p++ = ':';
        *p++ = ':';
        i += best_len;
        continue;
      }
      if (p != dst && p[-1] != ':')
        *p++ = ':';
      p += snprintf(p, (size_t)(dst + size - p), "%x", w[i++]);
    }
    if (p == dst) { // all zeros
      *p++ = ':';
      *p++ = ':';
    }
    *p = 0;
    return dst;
  }
  *__errno_location() = EAFNOSUPPORT;
  return NULL;
}

int inet_aton(const char *cp, struct in_addr *inp) {
  uint32_t v;
  if (!scan_v4(cp, &v, 0))
    return 0;
  if (inp)
    inp->s_addr = htonl(v);
  return 1;
}

in_addr_t inet_addr(const char *cp) {
  struct in_addr in;
  if (!inet_aton(cp, &in))
    return INADDR_NONE;
  return in.s_addr;
}

in_addr_t inet_network(const char *cp) {
  uint32_t v;
  if (!scan_v4(cp, &v, 0))
    return INADDR_NONE;
  return v; // host byte order per man
}

struct in_addr inet_makeaddr(in_addr_t net, in_addr_t host) {
  struct in_addr in;
  if (net < 128)
    in.s_addr = htonl((net << 24) | (host & 0xffffff));
  else if (net < 65536)
    in.s_addr = htonl((net << 16) | (host & 0xffff));
  else
    in.s_addr = htonl((net << 8) | (host & 0xff));
  return in;
}

in_addr_t inet_lnaof(struct in_addr in) {
  uint32_t a = ntohl(in.s_addr);
  if ((a & 0x80000000) == 0)
    return a & 0xffffff; // class A
  if ((a & 0xc0000000) == 0x80000000)
    return a & 0xffff; // class B
  return a & 0xff; // class C and beyond
}

in_addr_t inet_netof(struct in_addr in) {
  uint32_t a = ntohl(in.s_addr);
  if ((a & 0x80000000) == 0)
    return (a >> 24) & 0xff; // class A
  if ((a & 0xc0000000) == 0x80000000)
    return (a >> 16) & 0xffff; // class B
  return (a >> 8) & 0xffffff; // class C and beyond
}

char *inet_ntoa(struct in_addr in) {
  static char  buf[INET_ADDRSTRLEN];
  uint8_t     *b = (uint8_t *)&in.s_addr; // byte access: endian-safe
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return buf;
}
