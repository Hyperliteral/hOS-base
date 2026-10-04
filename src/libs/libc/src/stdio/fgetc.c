#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

int fgetc(FILE *file) {
  if (!file) {
    return EOF;
  }
  int pushed = __cavos_pushback_peek(file);
  if (pushed >= 0) {
    __cavos_pushback_take(file);
    return pushed;
  }
  if (file->fd < 0 || (file->flags & CAVOS_F_NORD)) {
    file->flags |= CAVOS_F_ERR;
    return EOF;
  }
  unsigned char c;
  long got = __cavos_ret((unsigned long)__syscall3(SYS_read, file->fd,
                                                   (long)&c, 1));
  if (got < 0) {
    file->flags |= CAVOS_F_ERR;
    return EOF;
  }
  if (got == 0) {
    file->flags |= CAVOS_F_EOF;
    return EOF;
  }
  return c;
}
