#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

// Name resolution without a DNS wire protocol (musl's resolver is a later
// milestone): numeric addresses and /etc/hosts-style literals resolve,
// everything else fails with EAI_NONAME. getaddrinfo callers that only need
// IP literals and localhost work today; apk-style DNS does not - yet.
static int parse_numeric(const char *node, struct sockaddr *addr,
                         socklen_t *len) {
  // Dotted quad.
  unsigned parts[4];
  const char *p = node;
  for (int i = 0; i < 4; i++) {
    if (*p < '0' || *p > '9')
      return -1;
    unsigned v = 0;
    while (*p >= '0' && *p <= '9') {
      v = v * 10 + (unsigned)(*p - '0');
      if (v > 255)
        return -1;
      p++;
    }
    parts[i] = v;
    if (i < 3) {
      if (*p != '.')
        return -1;
      p++;
    }
  }
  if (*p)
    return -1;
  if (addr && len && *len >= (socklen_t)sizeof(struct sockaddr_in)) {
    struct sockaddr_in *in = (struct sockaddr_in *)addr;
    in->sin_family = AF_INET;
    in->sin_port = 0;
    in->sin_addr.s_addr = (uint32_t)(parts[0] << 24 | parts[1] << 16 |
                                     parts[2] << 8 | parts[3]);
    in->sin_addr.s_addr = htonl(in->sin_addr.s_addr);
    *len = sizeof(struct sockaddr_in);
  }
  return 0;
}

int getaddrinfo(const char *restrict node, const char *restrict service,
                const struct addrinfo *restrict hints,
                struct addrinfo **restrict result) {
  (void)service;
  (void)hints;
  if (!node || !result)
    return EAI_NONAME;
  struct addrinfo *ai = malloc(sizeof(*ai) + sizeof(struct sockaddr_in));
  if (!ai)
    return EAI_MEMORY;
  struct sockaddr *sa = (struct sockaddr *)(ai + 1);
  socklen_t        len = sizeof(struct sockaddr_in);
  if (parse_numeric(node, sa, &len) < 0) {
    free(ai);
    return EAI_NONAME;
  }
  ai->ai_flags = 0;
  ai->ai_family = AF_INET;
  ai->ai_socktype = 0;
  ai->ai_protocol = 0;
  ai->ai_addrlen = len;
  ai->ai_addr = sa;
  ai->ai_canonname = 0;
  ai->ai_next = 0;
  *result = ai;
  return 0;
}
