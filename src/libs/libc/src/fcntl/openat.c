#include <fcntl.h>
#include <stdarg.h>

#include "internal/syscall.h"

int openat(int dirfd, const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, mode_t);
    va_end(args);
  }
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_openat, dirfd,
                                                   (long)path, flags,
                                                   (long)mode));
}
