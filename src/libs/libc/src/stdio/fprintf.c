#include <stdarg.h>
#include <stdio.h>

int fprintf(FILE *file, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfprintf(file, format, args);
  va_end(args);
  return result;
}
