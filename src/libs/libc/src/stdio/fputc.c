#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

int fputc(int c, FILE *file) {
  if (!file || file->fd < 0 || (file->flags & CAVOS_F_NOWR)) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return EOF;
  }
  unsigned char byte = (unsigned char)c;
  long wrote = __cavos_ret((unsigned long)__syscall3(SYS_write, file->fd,
                                                     (long)&byte, 1));
  if (wrote <= 0) {
    file->flags |= CAVOS_F_ERR;
    return EOF;
  }
  return byte;
}
