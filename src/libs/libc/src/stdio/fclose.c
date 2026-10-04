#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

int fclose(FILE *file) {
  if (!file) {
    *__errno_location() = EINVAL;
    return -1;
  }
  // Unbuffered: nothing to flush. Permanent streams (stdin/out/err) stay
  // open and allocated, exactly like musl's fclose treats F_PERM.
  if (file->flags & CAVOS_F_PERM)
    return 0;
  __cavos_pushback_drop(file);
  __cavos_pushback_drop(file);
  int result = 0;
  if (file->fd >= 0)
    result = (int)__cavos_ret((unsigned long)__syscall1(SYS_close, file->fd));
  file->fd = -1;
  free(file);
  return result;
}
