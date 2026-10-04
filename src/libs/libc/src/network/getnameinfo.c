#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

// Numeric-only getnameinfo (no DNS wire protocol, matching the getaddrinfo.c
// milestone): NI_NUMERICHOST/NI_NUMERICSERV are fast paths via
// inet_ntop/snprintf. Without flags a reverse lookup is attempted against
// /etc/hosts; anything needing real DNS returns EAI_NONAME. Service names
// fall back to the decimal port when /etc/services has no match.

static int reverse_hosts(const char *ip, char *host, socklen_t hostlen) {
  FILE *f = fopen("/etc/hosts", "r");
  if (!f)
    return -1;
  char line[512];
  int  found = -1;
  while (fgets(line, sizeof(line), f)) {
    char *hash = strchr(line, '#');
    if (hash)
      *hash = 0;
    char *addr = line;
    while (*addr == ' ' || *addr == '\t')
      addr++;
    if (!*addr || *addr == '\n')
      continue;
    char *end = addr;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n')
      end++;
    if (!*end)
      continue;
    *end++ = 0;
    if (strcmp(addr, ip) != 0)
      continue;
    while (*end == ' ' || *end == '\t')
      end++;
    char *name = end;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n')
      end++;
    *end = 0;
    if (!*name)
      continue;
    if (strlen(name) + 1 > hostlen) {
      found = -2;
      break;
    }
    strcpy(host, name);
    found = 0;
    break;
  }
  fclose(f);
  return found;
}

int getnameinfo(const struct sockaddr *restrict sa, socklen_t salen,
                char *restrict host, socklen_t hostlen, char *restrict serv,
                socklen_t servlen, int flags) {
  const uint8_t *addr_bytes = NULL;
  int            af = sa ? sa->sa_family : AF_UNSPEC;
  uint16_t       port = 0;

  if (af == AF_INET) {
    if (salen < (socklen_t)sizeof(struct sockaddr_in))
      return EAI_FAMILY;
    const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
    addr_bytes = (const uint8_t *)&in->sin_addr;
    port = ntohs(in->sin_port);
  } else if (af == AF_INET6) {
    if (salen < (socklen_t)sizeof(struct sockaddr_in6))
      return EAI_FAMILY;
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
    addr_bytes = in6->sin6_addr.s6_addr;
    port = ntohs(in6->sin6_port);
  } else {
    return EAI_FAMILY;
  }

  if (host && hostlen > 0) {
    char num[INET6_ADDRSTRLEN];
    if (!inet_ntop(af, addr_bytes, num, sizeof(num)))
      return EAI_SYSTEM;
    if (flags & NI_NUMERICHOST) {
      if (strlen(num) + 1 > hostlen)
        return EAI_OVERFLOW;
      strcpy(host, num);
    } else {
      int r = reverse_hosts(num, host, hostlen);
      if (r == -2)
        return EAI_OVERFLOW;
      if (r < 0) {
        if (flags & NI_NAMEREQD)
          return EAI_NONAME;
        // No hosts entry and no DNS: numeric fallback still succeeds.
        if (strlen(num) + 1 > hostlen)
          return EAI_OVERFLOW;
        strcpy(host, num);
      }
    }
  } else if (host) {
    return EAI_OVERFLOW;
  }

  if (serv && servlen > 0) {
    if (flags & NI_NUMERICSERV) {
      if (snprintf(serv, servlen, "%u", port) >= (int)servlen)
        return EAI_OVERFLOW;
    } else {
      struct servent *se =
          getservbyport(htons(port), NULL); // /etc/services reverse
      if (se) {
        if (strlen(se->s_name) + 1 > servlen)
          return EAI_OVERFLOW;
        strcpy(serv, se->s_name);
      } else if (snprintf(serv, servlen, "%u", port) >= (int)servlen) {
        return EAI_OVERFLOW;
      }
    }
  } else if (serv) {
    return EAI_OVERFLOW;
  }

  return 0;
}
