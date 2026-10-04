#include <stdarg.h>
#include <stdio.h>

int sprintf(char *s, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsprintf(s, format, args);
  va_end(args);
  return result;
}
