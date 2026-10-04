// off_t twin of fseek: the syscall takes 64-bit offsets natively, so no
// precision is lost the way the long-based fseek can lose it.
#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

int fseeko(FILE *file, off_t offset, int whence) {
  if (!file || file->fd < 0) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return -1;
  }
  long result = __cavos_ret((unsigned long)__syscall3(
      SYS_lseek, file->fd, (long)offset, whence));
  if (result < 0)
    return -1;
  file->flags &= ~CAVOS_F_EOF;
  return 0;
}
