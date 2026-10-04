// getdelim/getline over fgetc with realloc growth. Unbuffered core means
// one syscall per byte; correct for scripts and config parsing.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

ssize_t getdelim(char **restrict lineptr, size_t *restrict n, int delim,
                 FILE *restrict stream) {
  if (!lineptr || !n || !stream) {
    *__errno_location() = EINVAL;
    return -1;
  }
  size_t used = 0;
  if (!*lineptr || *n == 0) {
    *n = 128;
    *lineptr = malloc(*n);
    if (!*lineptr) {
      *n = 0;
      return -1;
    }
  }
  for (;;) {
    int c = fgetc(stream);
    if (c == EOF) {
      if (used == 0)
        return -1;
      break;
    }
    if (used + 2 > *n) {
      size_t grown = *n * 2;
      if (grown < *n + 128)
        grown = *n + 128;
      char *next = realloc(*lineptr, grown);
      if (!next) {
        *__errno_location() = ENOMEM;
        return -1;
      }
      *lineptr = next;
      *n = grown;
    }
    (*lineptr)[used++] = (char)c;
    if (c == delim)
      break;
  }
  (*lineptr)[used] = '\0';
  return (ssize_t)used;
}

ssize_t getline(char **restrict lineptr, size_t *restrict n,
                FILE *restrict stream) {
  return getdelim(lineptr, n, '\n', stream);
}
