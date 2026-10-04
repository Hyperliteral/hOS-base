#include <errno.h>
#include <netdb.h>
#include <string.h>

// Reentrant /etc/services lookup. NOTE: the non-reentrant
// setservent/endservent/getservent/getservbyname/getservbyport already live
// in services.c and are deliberately NOT redefined here (that would be a
// duplicate symbol). These _r variants walk that same database and pack the
// match into the caller's buffer: ERANGE when too small, ENOENT on a miss.

static int pack_servent(struct servent *se, char *buf, size_t buflen,
                        struct servent **result, const struct servent *src) {
  size_t need = strlen(src->s_name) + 1 + strlen(src->s_proto) + 1;
  int    nalias = 0;
  for (char **a = src->s_aliases; a && *a; a++) {
    need += strlen(*a) + 1;
    nalias++;
  }
  need += (size_t)(nalias + 1) * sizeof(char *);
  if (need > buflen) {
    *result = NULL;
    return ERANGE;
  }
  char *p = buf;
  se->s_name = p;
  size_t l = strlen(src->s_name) + 1;
  memcpy(p, src->s_name, l);
  p += l;
  se->s_proto = p;
  l = strlen(src->s_proto) + 1;
  memcpy(p, src->s_proto, l);
  p += l;
  se->s_aliases = (char **)p;
  p += (size_t)(nalias + 1) * sizeof(char *);
  for (int i = 0; i < nalias; i++) {
    se->s_aliases[i] = p;
    l = strlen(src->s_aliases[i]) + 1;
    memcpy(p, src->s_aliases[i], l);
    p += l;
  }
  se->s_aliases[nalias] = NULL;
  se->s_port = src->s_port;
  *result = se;
  return 0;
}

int getservbyname_r(const char *name, const char *proto, struct servent *se,
                    char *buf, size_t buflen, struct servent **result) {
  setservent(0);
  struct servent *cur;
  int             rc = ENOENT;
  *result = NULL;
  while ((cur = getservent()) != NULL) {
    if (strcmp(cur->s_name, name) != 0)
      continue;
    if (proto && strcmp(cur->s_proto, proto) != 0)
      continue;
    rc = pack_servent(se, buf, buflen, result, cur);
    break;
  }
  endservent();
  return rc;
}

int getservbyport_r(int port, const char *proto, struct servent *se,
                    char *buf, size_t buflen, struct servent **result) {
  setservent(0);
  struct servent *cur;
  int             rc = ENOENT;
  *result = NULL;
  while ((cur = getservent()) != NULL) {
    if (cur->s_port != port)
      continue;
    if (proto && strcmp(cur->s_proto, proto) != 0)
      continue;
    rc = pack_servent(se, buf, buflen, result, cur);
    break;
  }
  endservent();
  return rc;
}
