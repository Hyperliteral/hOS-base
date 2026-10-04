#include <unistd.h>

#include "internal/syscall.h"

int unlinkat(int dirfd, const char *path, int flags) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_unlinkat, dirfd, (long)path, flags));
}
