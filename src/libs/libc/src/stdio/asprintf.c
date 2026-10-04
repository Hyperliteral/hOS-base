#include <stdarg.h>
#include <stdio.h>

int asprintf(char **str, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vasprintf(str, format, args);
  va_end(args);
  return result;
}
