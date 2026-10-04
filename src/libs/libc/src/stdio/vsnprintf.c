#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "internal/printf_impl.h"

// POSIX semantics: NUL-terminates when n > 0 (at s[n-1] on truncation),
// leaves s alone when n == 0, and always returns the unbounded length.
int vsnprintf(char *s, size_t n, const char *format, va_list args) {
  size_t total = __cavos_format(s, n, format, args);
  if (n > 0)
    s[total < n ? total : n - 1] = '\0';
  return (int)total;
}
