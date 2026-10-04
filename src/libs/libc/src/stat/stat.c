#include <sys/stat.h>

#include "internal/syscall.h"

int stat(const char *restrict path, struct stat *restrict buf) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_stat, (long)path, (long)buf));
}
