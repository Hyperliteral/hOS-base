#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

long ftell(FILE *file) {
  if (!file || file->fd < 0) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return -1;
  }
  return __cavos_ret(
      (unsigned long)__syscall3(SYS_lseek, file->fd, 0, SEEK_CUR));
}
