#include <sys/socket.h>

#include "internal/syscall.h"

int getsockopt(int fd, int level, int optname, void *restrict optval,
               socklen_t *restrict optlen) {
  return (int)__cavos_ret((unsigned long)__syscall5(SYS_getsockopt, fd, level,
                                                   optname, (long)optval,
                                                   (long)optlen));
}
