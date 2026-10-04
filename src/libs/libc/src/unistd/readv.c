#include <sys/uio.h>

#include "internal/syscall.h"

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
  return (ssize_t)__cavos_ret(
      (unsigned long)__syscall3(SYS_readv, fd, (long)iov, iovcnt));
}
