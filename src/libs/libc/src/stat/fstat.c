#include <sys/stat.h>

#include "internal/syscall.h"

int fstat(int fd, struct stat *buf) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_fstat, fd, (long)buf));
}
