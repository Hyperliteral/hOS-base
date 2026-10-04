#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "internal/scanf_impl.h"

typedef struct StringSrc {
  const char *p;
} StringSrc;

static int string_getc(void *ctx) {
  StringSrc *src = (StringSrc *)ctx;
  if (!*src->p)
    return EOF;
  return (unsigned char)*src->p++;
}

static int string_ungetc(int c, void *ctx) {
  StringSrc *src = (StringSrc *)ctx;
  if (c == EOF || src->p == 0)
    return EOF;
  // The engine only ever returns characters it just consumed.
  src->p--;
  return c;
}

int vsscanf(const char *restrict s, const char *restrict format,
            va_list args) {
  StringSrc src;
  src.p = s;
  return __cavos_vfscanf(&src, string_getc, string_ungetc, format, args);
}

int sscanf(const char *restrict s, const char *restrict format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsscanf(s, format, args);
  va_end(args);
  return result;
}
