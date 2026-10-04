#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "internal/printf_impl.h"

// Unbounded target: SIZE_MAX capacity never truncates in practice, and the
// engine NUL-terminates... except it doesn't NUL-terminate at all (caller's
// vsnprintf does). Handle NUL here from the returned length.
int vsprintf(char *s, const char *format, va_list args) {
  // Probe the length first without writing anywhere observable: format
  // into the target with an unbounded cap, then terminate.
  size_t total = __cavos_format(s, SIZE_MAX - 1, format, args);
  s[total] = 0;
  return (int)total;
}
