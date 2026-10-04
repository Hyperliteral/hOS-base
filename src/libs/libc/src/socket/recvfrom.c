#include <sys/socket.h>

#include "internal/syscall.h"

ssize_t recvfrom(int fd, void *restrict buf, size_t len, int flags,
                 struct sockaddr *restrict addr, socklen_t *restrict addrlen) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall6(
      SYS_recvfrom, fd, (long)buf, len, flags, (long)addr, (long)addrlen));
}
