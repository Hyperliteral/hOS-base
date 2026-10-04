#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *file) {
  if (!ptr || !file || file->fd < 0 || size == 0 || nmemb == 0 ||
      (file->flags & CAVOS_F_NORD))
    return 0;
  // Unbuffered: read whole items directly. Short items only happen at EOF
  // or on error, so item accounting stays exact.
  char *out = (char *)ptr;
  size_t done = 0;
  // A pushed-back byte reads first, per stream order.
  int pushed = __cavos_pushback_peek(file);
  if (pushed >= 0 && size > 0 && nmemb > 0) {
    __cavos_pushback_take(file);
    out[0] = (char)pushed;
    if (size == 1) {
      done = 1;
      if (done >= nmemb)
        return done;
    } else {
      // Partial first item: fall through to the main loop, which
      // continues the item at offset 1.
      size_t have = 1;
      while (have < size) {
        long got = __cavos_ret((unsigned long)__syscall3(
            SYS_read, file->fd, (long)(out + have), size - have));
        if (got < 0) {
          file->flags |= CAVOS_F_ERR;
          return 0;
        }
        if (got == 0) {
          file->flags |= CAVOS_F_EOF;
          return 0;
        }
        have += (size_t)got;
      }
      done = 1;
    }
  }
  while (done < nmemb) {
    size_t want = size;
    while (want > 0) {
      long got = __cavos_ret((unsigned long)__syscall3(
          SYS_read, file->fd, (long)(out + done * size + (size - want)), want));
      if (got < 0) {
        file->flags |= CAVOS_F_ERR;
        return done;
      }
      if (got == 0) {
        file->flags |= CAVOS_F_EOF;
        return done;
      }
      want -= (size_t)got;
    }
    done++;
  }
  return done;
}
