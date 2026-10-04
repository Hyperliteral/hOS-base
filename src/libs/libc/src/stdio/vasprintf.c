#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "internal/printf_impl.h"

// Size probe (no write), allocate, format. Two engine passes; the format
// string is side-effect free to evaluate twice.
int vasprintf(char **str, const char *format, va_list args) {
  va_list probe;
  va_copy(probe, args);
  size_t total = __cavos_format(0, 0, format, probe);
  va_end(probe);
  char *out = malloc(total + 1);
  if (!out)
    return -1;
  size_t again = __cavos_format(out, total + 1, format, args);
  out[again < total + 1 ? again : total] = 0;
  *str = out;
  return (int)total;
}
