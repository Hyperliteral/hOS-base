#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

int snprintf(char *s, size_t n, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(s, n, format, args);
  va_end(args);
  return result;
}
