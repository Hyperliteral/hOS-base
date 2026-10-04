#include <sys/select.h>

#include "internal/syscall.h"

int pselect(int nfds, fd_set *restrict readfds, fd_set *restrict writefds,
            fd_set *restrict errorfds, const struct timespec *restrict timeout,
            const sigset_t *restrict mask) {
  return (int)__cavos_ret((unsigned long)__syscall6(
      SYS_pselect6, nfds, (long)readfds, (long)writefds, (long)errorfds,
      (long)timeout, (long)mask));
}
