#include <stdarg.h>
#include <stdio.h>

int scanf(const char *restrict format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfscanf(stdin, format, args);
  va_end(args);
  return result;
}
