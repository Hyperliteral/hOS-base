#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

// Legacy numeric host lookup (musl-style numeric fallback): dotted-quad
// literals resolve into a static hostent; anything else returns NULL with
// h_errno HOST_NOT_FOUND (no DNS wire protocol yet - see getaddrinfo.c).
// gethostent/sethostent/endhostent do not enumerate: NULL/no-op.

static int              he_errno_storage;
static char             he_name[INET_ADDRSTRLEN];
static struct in_addr   he_addrval;
static char            *he_aliases[1];
static char            *he_addr_list[2];
static struct hostent   he_result;

int *__h_errno_location(void) { return &he_errno_storage; }

static struct hostent *fill_numeric(const char *dotted) {
  struct in_addr in;
  if (!inet_aton(dotted, &in)) {
    he_errno_storage = HOST_NOT_FOUND;
    return NULL;
  }
  snprintf(he_name, sizeof(he_name), "%s", dotted);
  he_addrval = in;
  he_aliases[0] = NULL;
  he_addr_list[0] = (char *)&he_addrval;
  he_addr_list[1] = NULL;
  he_result.h_name = he_name;
  he_result.h_aliases = he_aliases;
  he_result.h_addrtype = AF_INET;
  he_result.h_length = 4;
  he_result.h_addr_list = he_addr_list;
  he_errno_storage = 0;
  return &he_result;
}

struct hostent *gethostbyname(const char *name) {
  if (!name) {
    he_errno_storage = HOST_NOT_FOUND;
    return NULL;
  }
  return fill_numeric(name);
}

struct hostent *gethostbyname2(const char *name, int af) {
  if (af != AF_INET) { // no IPv6 database behind the numeric fallback
    he_errno_storage = HOST_NOT_FOUND;
    return NULL;
  }
  return gethostbyname(name);
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) {
  if (type != AF_INET || !addr || len < (socklen_t)sizeof(struct in_addr)) {
    he_errno_storage = HOST_NOT_FOUND;
    return NULL;
  }
  struct in_addr in;
  memcpy(&in, addr, sizeof(in));
  char *dotted = inet_ntoa(in); // static buffer: copy before reuse
  char  tmp[INET_ADDRSTRLEN];
  snprintf(tmp, sizeof(tmp), "%s", dotted);
  return fill_numeric(tmp);
}

int gethostbyname_r(const char *name, struct hostent *ret, char *buf,
                    size_t buflen, struct hostent **result, int *h_errnop) {
  struct hostent *h = gethostbyname(name);
  if (!h) {
    if (h_errnop)
      *h_errnop = he_errno_storage;
    if (result)
      *result = NULL;
    return he_errno_storage;
  }
  size_t need = strlen(h->h_name) + 1 + sizeof(struct in_addr) +
                sizeof(he_aliases) + sizeof(he_addr_list);
  if (need > buflen) {
    if (h_errnop)
      *h_errnop = NO_RECOVERY;
    if (result)
      *result = NULL;
    return ERANGE;
  }
  char *p = buf;
  ret->h_name = p;
  strcpy(p, h->h_name);
  p += strlen(p) + 1;
  ret->h_aliases = (char **)p;
  ret->h_aliases[0] = NULL;
  p += sizeof(he_aliases);
  ret->h_addr_list = (char **)p;
  p += sizeof(he_addr_list);
  ret->h_addr_list[0] = p;
  memcpy(p, h->h_addr_list[0], sizeof(struct in_addr));
  ret->h_addr_list[1] = NULL;
  ret->h_addrtype = AF_INET;
  ret->h_length = 4;
  if (h_errnop)
    *h_errnop = 0;
  if (result)
    *result = ret;
  return 0;
}

int gethostbyname2_r(const char *name, int af, struct hostent *ret, char *buf,
                     size_t buflen, struct hostent **result, int *h_errnop) {
  if (af != AF_INET) {
    if (h_errnop)
      *h_errnop = HOST_NOT_FOUND;
    if (result)
      *result = NULL;
    return HOST_NOT_FOUND;
  }
  return gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
}

int gethostbyaddr_r(const void *addr, socklen_t len, int type,
                    struct hostent *ret, char *buf, size_t buflen,
                    struct hostent **result, int *h_errnop) {
  struct hostent *h = gethostbyaddr(addr, len, type);
  if (!h) {
    if (h_errnop)
      *h_errnop = he_errno_storage;
    if (result)
      *result = NULL;
    return he_errno_storage;
  }
  char tmp[INET_ADDRSTRLEN];
  snprintf(tmp, sizeof(tmp), "%s", h->h_name);
  size_t need = strlen(tmp) + 1 + sizeof(struct in_addr) +
                sizeof(he_aliases) + sizeof(he_addr_list);
  if (need > buflen) {
    if (h_errnop)
      *h_errnop = NO_RECOVERY;
    if (result)
      *result = NULL;
    return ERANGE;
  }
  char *p = buf;
  ret->h_name = p;
  strcpy(p, tmp);
  p += strlen(p) + 1;
  ret->h_aliases = (char **)p;
  ret->h_aliases[0] = NULL;
  p += sizeof(he_aliases);
  ret->h_addr_list = (char **)p;
  p += sizeof(he_addr_list);
  ret->h_addr_list[0] = p;
  memcpy(p, &he_addrval, sizeof(struct in_addr));
  ret->h_addr_list[1] = NULL;
  ret->h_addrtype = AF_INET;
  ret->h_length = 4;
  if (h_errnop)
    *h_errnop = 0;
  if (result)
    *result = ret;
  return 0;
}

void sethostent(int stayopen) {
  (void)stayopen; // no enumeration backend: nothing to keep open
}

void endhostent(void) { // symmetric no-op
}

struct hostent *gethostent(void) {
  return NULL; // no hosts enumeration yet; DNS-backed iteration is future work
}

void herror(const char *s) {
  const char *msg = hstrerror(he_errno_storage);
  if (s && *s)
    fprintf(stderr, "%s: %s\n", s, msg);
  else
    fprintf(stderr, "%s\n", msg);
}

const char *hstrerror(int err) {
  switch (err) {
  case HOST_NOT_FOUND:
    return "Host not found";
  case TRY_AGAIN:
    return "Try again";
  case NO_RECOVERY:
    return "No recovery";
  case NO_DATA:
    return "No data";
  default:
    return "Unknown host error";
  }
}
