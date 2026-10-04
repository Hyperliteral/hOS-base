#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *file) {
  if (!ptr || !file || file->fd < 0 || size == 0 || nmemb == 0 ||
      (file->flags & CAVOS_F_NOWR))
    return 0;
  const char *in = (const char *)ptr;
  size_t done = 0;
  while (done < nmemb) {
    size_t want = size;
    while (want > 0) {
      long got = __cavos_ret((unsigned long)__syscall3(
          SYS_write, file->fd, (long)(in + done * size + (size - want)), want));
      if (got <= 0) {
        file->flags |= CAVOS_F_ERR;
        return done;
      }
      want -= (size_t)got;
    }
    done++;
  }
  return done;
}
