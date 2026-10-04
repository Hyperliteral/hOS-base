#include <sys/stat.h>

#include "internal/syscall.h"

int fchmod(int fd, mode_t mode) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_fchmod, fd, (long)mode));
}
