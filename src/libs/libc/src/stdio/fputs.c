#include <stdio.h>
#include <string.h>

int fputs(const char *restrict s, FILE *restrict file) {
  size_t length = strlen(s);
  if (fwrite(s, 1, length, file) != length)
    return EOF;
  return (int)length;
}
