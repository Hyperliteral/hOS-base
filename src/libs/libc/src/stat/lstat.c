#include <sys/stat.h>

#include "internal/syscall.h"

int lstat(const char *restrict path, struct stat *restrict buf) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_lstat, (long)path, (long)buf));
}
