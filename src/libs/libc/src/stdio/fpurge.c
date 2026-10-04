#include <errno.h>
#include <stdio.h>

#include "internal/stdio_impl.h"

// musl's __fpurge discards buffered input; with no buffer there is nothing
// to discard, so this is clearerr (which is exactly what it degrades to).
void __fpurge(FILE *file) {
  if (file)
    clearerr(file);
}
