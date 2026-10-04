#include <stdio.h>
#include <unistd.h>

int putchar(int character) {
  char byte = (char)character;
  if (write(STDOUT_FILENO, &byte, 1) < 0)
    return EOF;
  return character;
}
