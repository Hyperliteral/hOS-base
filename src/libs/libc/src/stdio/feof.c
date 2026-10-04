#include <stdio.h>

#include "internal/stdio_impl.h"

int feof(FILE *file) {
  if (!file)
    return 0;
  return (file->flags & CAVOS_F_EOF) != 0;
}
