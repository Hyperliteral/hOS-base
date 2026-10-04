#include <unistd.h>

#include "internal/syscall.h"

ssize_t read(int fd, void *buffer, size_t count) {
  return (ssize_t)__cavos_ret(
      (unsigned long)__syscall3(SYS_read, fd, (long)buffer, (long)count));
}
