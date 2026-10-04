#include <sys/socket.h>

#include "internal/syscall.h"

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t addrlen) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall6(
      SYS_sendto, fd, (long)buf, len, flags, (long)addr, addrlen));
}
