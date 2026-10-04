#include <stdio.h>

#include "internal/stdio_impl.h"

static struct _IO_FILE stdin_storage = {
  .flags = CAVOS_F_PERM | CAVOS_F_NOWR,
  .fd = 0,
};

FILE *const stdin = &stdin_storage;
