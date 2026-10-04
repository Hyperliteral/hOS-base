#include <errno.h>
#include <stdio.h>

#include "internal/stdio_impl.h"

int fileno(FILE *file) {
  if (!file || file->fd < 0) {
    // musl returns -1 with EBADF; errno first.
    *__errno_location() = EBADF;
    return -1;
  }
  return file->fd;
}
