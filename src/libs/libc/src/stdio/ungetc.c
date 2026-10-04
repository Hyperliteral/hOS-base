#include <stdio.h>

#include "internal/stdio_impl.h"

// Single-slot pushback via the side table (see pushback.c): fails on EOF
// input or when a character is already pushed back; clears EOF on success.
int ungetc(int c, FILE *file) {
  if (!file || c == EOF)
    return EOF;
  if (__cavos_pushback_put(file, c) < 0)
    return EOF;
  file->flags &= ~CAVOS_F_EOF;
  return c;
}
