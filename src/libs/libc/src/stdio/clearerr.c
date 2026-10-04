#include <stdio.h>

#include "internal/stdio_impl.h"

void clearerr(FILE *file) {
  if (!file)
    return;
  file->flags &= ~(CAVOS_F_EOF | CAVOS_F_ERR);
}
