#include <stdio.h>

#include "internal/stdio_impl.h"

static struct _IO_FILE stdout_storage = {
  .flags = CAVOS_F_PERM | CAVOS_F_NORD,
  .fd = 1,
};

FILE *const stdout = &stdout_storage;
