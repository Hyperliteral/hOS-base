#include <unistd.h>

#include "internal/syscall.h"

// No kernel symlink yet: ENOSYS at runtime, linkable today.
int symlink(const char *target, const char *linkpath) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_symlink, (long)target, (long)linkpath));
}
