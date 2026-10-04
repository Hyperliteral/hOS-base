// off_t twin of ftell: query the offset without moving it.
#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

off_t ftello(FILE *file) {
  if (!file || file->fd < 0) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return -1;
  }
  long result = __cavos_ret(
      (unsigned long)__syscall3(SYS_lseek, file->fd, 0, SEEK_CUR));
  if (result < 0)
    return -1;
  return (off_t)result;
}
