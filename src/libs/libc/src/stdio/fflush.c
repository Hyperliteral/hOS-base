#include <stdio.h>

// Unbuffered libc: nothing to flush, always succeeds. Documented in
// src/internal/stdio_impl.h - do not "optimize" this into a real flush
// without adding a buffer to struct _IO_FILE first.
int fflush(FILE *file) {
  (void)file;
  return 0;
}
