#include <sys/socket.h>

#include "internal/syscall.h"

int accept4(int fd, struct sockaddr *restrict addr, socklen_t *restrict len,
            int flags) {
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_accept4, fd,
                                                   (long)addr, (long)len,
                                                   flags));
}
