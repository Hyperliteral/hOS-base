#include <fcntl.h>
#include <stdarg.h>

#include "internal/syscall.h"

// Implemented directly on SYS_open, which this kernel provides (Linux also
// keeps it for x86_64). Flags and mode values come from the vendored
// <fcntl.h> and already match the kernel's Linux-compatible table.
int open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, mode_t);
    va_end(args);
  }
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_open, (long)path, flags, (long)mode));
}
