#include <stdarg.h>
#include <sys/ioctl.h>

#include "internal/syscall.h"

// The kernel takes exactly one argument after the request; the variadic tail
// exists for C compatibility only. Callers passing no third argument leave
// whatever happens to be in the slot, which the kernel ignores for
// argument-less ioctls - same reasoning as syscall(), see unistd/syscall.c.
int ioctl(int fd, int request, ...) {
  va_list args;
  va_start(args, request);
  void *arg = va_arg(args, void *);
  va_end(args);
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_ioctl, fd, request, (long)arg));
}
