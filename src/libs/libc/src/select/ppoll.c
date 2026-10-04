#include <poll.h>
#include <signal.h>

#include "internal/syscall.h"

// The kernel takes the mask plus its size; _NSIG/8 matches the
// sigprocmask.c convention. The kernel applies the mask directly, so this
// is a straight passthrough with no userspace emulation.
int ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
          const sigset_t *mask) {
  return (int)__cavos_ret((unsigned long)__syscall5(
      SYS_ppoll, (long)fds, count, (long)timeout, (long)mask, _NSIG / 8));
}
