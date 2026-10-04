#include <sys/socket.h>

#include "internal/syscall.h"

int bind(int fd, const struct sockaddr *addr, socklen_t len) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_bind, fd, (long)addr, len));
}
