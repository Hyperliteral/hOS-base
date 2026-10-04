#include <unistd.h>

#include "internal/syscall.h"

off_t lseek(int fd, off_t offset, int whence) {
  return (off_t)__cavos_ret(
      (unsigned long)__syscall3(SYS_lseek, fd, (long)offset, whence));
}
