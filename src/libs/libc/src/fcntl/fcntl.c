#include <fcntl.h>
#include <stdarg.h>

#include "internal/syscall.h"

// Only F_DUPFD-style commands take an argument here; the kernel ignores
// extras for the rest, so forwarding unconditionally is safe.
int fcntl(int fd, int cmd, ...) {
  va_list args;
  va_start(args, cmd);
  long arg = va_arg(args, long);
  va_end(args);
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_fcntl, fd, cmd, arg));
}
