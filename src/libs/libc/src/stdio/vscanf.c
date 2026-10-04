#include <stdarg.h>
#include <stdio.h>

int vscanf(const char *restrict format, va_list args) {
  return vfscanf(stdin, format, args);
}
