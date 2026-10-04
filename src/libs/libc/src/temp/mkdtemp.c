#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "internal/syscall.h"

// Template filler sharedconceptually with mkstemp.c/mktemp.c (duplicated,
// not shared: static linkage keeps every object dependency-free).
static const char digits[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static int fill_template(char *tmpl) {
  size_t len = strlen(tmpl);
  if (len < 6)
    goto bad;
  for (size_t i = len - 6; i < len; i++) {
    if (tmpl[i] != 'X')
      goto bad;
  }
  unsigned long long rnd = 0;
  if (__cavos_ret((unsigned long)__syscall3(SYS_getrandom, (long)&rnd,
                                           sizeof(rnd), 0)) != sizeof(rnd)) {
    rnd = (unsigned long long)(uintptr_t)&rnd ^ (unsigned long long)time(0) ^
          ((unsigned long long)getpid() << 32);
  }
  for (size_t i = len - 6; i < len; i++) {
    rnd = rnd * 6364136223846793005ULL + 1442695040888963407ULL;
    tmpl[i] = digits[(rnd >> 33) % 62];
  }
  return 0;
bad:
  *__errno_location() = EINVAL;
  return -1;
}

char *mkdtemp(char *tmpl) {
  // mkdir() itself is the atomic create-if-absent: retry on collision.
  for (int tries = 0; tries < 100; tries++) {
    char attempt[256];
    size_t len = strlen(tmpl);
    if (len >= sizeof(attempt)) {
      *__errno_location() = EINVAL;
      return 0;
    }
    strcpy(attempt, tmpl);
    if (fill_template(attempt) < 0)
      return 0;
    if (mkdir(attempt, 0700) == 0) {
      strcpy(tmpl, attempt);
      return tmpl;
    }
    if (*__errno_location() != EEXIST)
      return 0;
  }
  *__errno_location() = EEXIST;
  return 0;
}
