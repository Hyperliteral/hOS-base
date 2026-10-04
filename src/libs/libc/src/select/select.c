#include <sys/select.h>

#include "internal/syscall.h"

int select(int nfds, fd_set *restrict readfds, fd_set *restrict writefds,
           fd_set *restrict errorfds, struct timeval *restrict timeout) {
  return (int)__cavos_ret(
      (unsigned long)__syscall5(SYS_select, nfds, (long)readfds,
                               (long)writefds, (long)errorfds, (long)timeout));
}
