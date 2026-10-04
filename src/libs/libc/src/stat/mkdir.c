#include <sys/stat.h>

#include "internal/syscall.h"

int mkdir(const char *path, mode_t mode) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_mkdir, (long)path, (long)mode));
}
