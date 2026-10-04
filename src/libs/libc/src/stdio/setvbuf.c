#include <stdio.h>

// Unbuffered libc: every mode is already "unbuffered", so any valid request
// succeeds trivially. Invalid mode/buffer-size combos fail like musl's.
int setvbuf(FILE *restrict file, char *restrict buf, int mode, size_t size) {
  (void)buf;
  (void)size;
  if (!file)
    return -1;
  if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)
    return -1;
  return 0;
}
