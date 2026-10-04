#include <sys/uio.h>

#include "internal/syscall.h"

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  return (ssize_t)__cavos_ret(
      (unsigned long)__syscall3(SYS_writev, fd, (long)iov, iovcnt));
}
