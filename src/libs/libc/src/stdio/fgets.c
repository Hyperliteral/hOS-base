#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

char *fgets(char *s, int n, FILE *file) {
  if (!s || n <= 0 || !file || file->fd < 0 ||
      (file->flags & CAVOS_F_NORD)) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return 0;
  }
  // Unbuffered: one byte at a time straight from the fd (plus pushback).
  // Slow for megabytes, exactly right for config files.
  int i = 0;
  while (i < n - 1) {
    int c = __cavos_pushback_peek(file);
    if (c >= 0) {
      __cavos_pushback_take(file);
    } else {
      unsigned char b;
      long got = __cavos_ret((unsigned long)__syscall3(SYS_read, file->fd,
                                                      (long)&b, 1));
      if (got < 0) {
        file->flags |= CAVOS_F_ERR;
        break;
      }
      if (got == 0) {
        file->flags |= CAVOS_F_EOF;
        break;
      }
      c = b;
    }
    s[i++] = (char)c;
    if (c == '\n')
      break;
  }
  if (i == 0)
    return 0;
  s[i] = '\0';
  return s;
}
