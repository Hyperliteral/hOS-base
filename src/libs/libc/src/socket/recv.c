#include <sys/socket.h>

#include "internal/syscall.h"

// No dedicated SYS_recv: recv is recvfrom with no source address.
ssize_t recv(int fd, void *buf, size_t len, int flags) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall6(SYS_recvfrom, fd,
                                                       (long)buf, len, flags,
                                                       0, 0));
}
