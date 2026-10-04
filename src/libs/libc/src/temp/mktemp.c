#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "internal/syscall.h"

// Template filler, same duplication note as in mkstemp.c/mkdtemp.c.
static const char digits[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// mktemp is inherently racy (name without creation); it exists for ancient
// callers only. Prefer mkstemp/mkdtemp. Still provided since bash links it.
char *mktemp(char *tmpl) {
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
  return tmpl;
bad:
  tmpl[0] = 0;
  *__errno_location() = EINVAL;
  return tmpl;
}
