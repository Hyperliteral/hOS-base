#include <stdio.h>

#include "internal/stdio_impl.h"

int ferror(FILE *file) {
  if (!file)
    return 0;
  return (file->flags & CAVOS_F_ERR) != 0;
}
