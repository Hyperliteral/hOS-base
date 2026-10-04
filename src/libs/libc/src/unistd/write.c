#include <unistd.h>

#include "internal/syscall.h"

ssize_t write(int fd, const void *buffer, size_t count) {
  return (ssize_t)__cavos_ret(
      (unsigned long)__syscall3(SYS_write, fd, (long)buffer, (long)count));
}
