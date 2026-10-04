#include <stdio.h>

#include "internal/stdio_impl.h"

static struct _IO_FILE stderr_storage = {
  .flags = CAVOS_F_PERM | CAVOS_F_NORD,
  .fd = 2,
};

FILE *const stderr = &stderr_storage;
