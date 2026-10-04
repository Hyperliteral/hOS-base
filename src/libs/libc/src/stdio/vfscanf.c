#include <stdarg.h>
#include <stdio.h>

#include "internal/scanf_impl.h"
#include "internal/stdio_impl.h"
#include "internal/syscall.h"

typedef struct FileSrc {
  FILE *file;
} FileSrc;

static int file_getc(void *ctx) {
  FILE *file = ((FileSrc *)ctx)->file;
  int   pushed = __cavos_pushback_peek(file);
  if (pushed >= 0) {
    __cavos_pushback_take(file);
    return pushed;
  }
  if (file->fd < 0) {
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

static int file_ungetc(int c, void *ctx) {
  FILE *file = ((FileSrc *)ctx)->file;
  if (c == EOF)
    return EOF;
  if (__cavos_pushback_put(file, c) < 0)
    return EOF;
  file->flags &= ~CAVOS_F_EOF;
  return c;
}

int vfscanf(FILE *restrict file, const char *restrict format, va_list args) {
  FileSrc src;
  src.file = file;
  return __cavos_vfscanf(&src, file_getc, file_ungetc, format, args);
}

int fscanf(FILE *restrict file, const char *restrict format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfscanf(file, format, args);
  va_end(args);
  return result;
}
