#include <limits.h>
#include <stdio.h>
#include <unistd.h>

// Terminal name via /proc/self/fd, like musl does. NoReadable proc means no
// name (ENOTTY-style failure path per POSIX: return NULL with ENOTTY).
char *ttyname(int fd) {
  static char buf[128];
  char        link[64];
  int         n = snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
  if (n < 0 || (size_t)n >= sizeof(link))
    return 0;
  ssize_t got = readlink(link, buf, sizeof(buf) - 1);
  if (got < 0)
    return 0;
  buf[got] = 0;
  return buf;
}
