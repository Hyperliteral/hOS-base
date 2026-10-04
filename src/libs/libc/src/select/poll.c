#include <poll.h>

#include "internal/syscall.h"

int poll(struct pollfd *fds, nfds_t count, int timeout) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_poll, (long)fds, count, timeout));
}
