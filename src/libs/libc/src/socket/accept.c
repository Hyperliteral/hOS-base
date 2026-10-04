#include <sys/socket.h>

#include "internal/syscall.h"

int accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_accept, fd, (long)addr,
                                                   (long)len));
}
