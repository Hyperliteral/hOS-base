#include <sys/socket.h>

#include "internal/syscall.h"

int listen(int fd, int backlog) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_listen, fd, backlog));
}
