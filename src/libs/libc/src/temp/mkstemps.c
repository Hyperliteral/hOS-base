// mkstemps: mkstemp with a suffix preserved past the XXXXXX block
// (mirrors mkstemp.c's template engine; suffixlen counts the suffix bytes).
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "internal/syscall.h"

static const char digits[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static int fill_template(char *tmpl, int suffixlen) {
  size_t len = strlen(tmpl);
  if (suffixlen < 0 || (size_t)suffixlen + 6 > len)
    goto bad;
  size_t end = len - (size_t)suffixlen;
  for (size_t i = end - 6; i < end; i++) {
    if (tmpl[i] != 'X')
      goto bad;
  }
  unsigned long long rnd = 0;
  if (__cavos_ret((unsigned long)__syscall3(SYS_getrandom, (long)&rnd,
                                            sizeof(rnd), 0)) != sizeof(rnd)) {
    rnd = (unsigned long long)(uintptr_t)&rnd ^ (unsigned long long)time(0) ^
          ((unsigned long long)getpid() << 32);
  }
  for (size_t i = end - 6; i < end; i++) {
    rnd = rnd * 6364136223846793005ULL + 1442695040888963407ULL;
    tmpl[i] = digits[(rnd >> 33) % 62];
  }
  return 0;
bad:
  *__errno_location() = EINVAL;
  return -1;
}

int mkstemps(char *tmpl, int suffixlen) {
  for (int tries = 0; tries < 100; tries++) {
    char attempt[256];
    size_t len = strlen(tmpl);
    if (len >= sizeof(attempt)) {
      *__errno_location() = EINVAL;
      return -1;
    }
    strcpy(attempt, tmpl);
    if (fill_template(attempt, suffixlen) < 0)
      return -1;
    int fd = (int)__cavos_ret((unsigned long)__syscall4(
        SYS_openat, AT_FDCWD, (long)attempt, O_RDWR | O_CREAT | O_EXCL,
        (long)0600));
    if (fd >= 0) {
      strcpy(tmpl, attempt);
      return fd;
    }
    if (*__errno_location() != EEXIST)
      return -1;
  }
  *__errno_location() = EEXIST;
  return -1;
}
