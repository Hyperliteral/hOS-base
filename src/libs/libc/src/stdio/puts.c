#include <stdio.h>
#include <string.h>
#include <unistd.h>

int puts(const char *text) {
  size_t length = strlen(text);
  if (write(STDOUT_FILENO, text, length) < 0)
    return EOF;
  if (write(STDOUT_FILENO, "\n", 1) < 0)
    return EOF;
  return (int)length;
}
