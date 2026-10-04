#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

int fseek(FILE *file, long offset, int whence) {
  if (!file || file->fd < 0) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return -1;
  }
  long result = __cavos_ret(
      (unsigned long)__syscall3(SYS_lseek, file->fd, offset, whence));
  if (result < 0)
    return -1;
  file->flags &= ~CAVOS_F_EOF;
  return 0;
}
