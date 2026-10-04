#include <signal.h>

#include "internal/syscall.h"

int sigprocmask(int how, const sigset_t *restrict set,
                sigset_t *restrict old) {
  return (int)__cavos_ret((unsigned long)__syscall4(
      SYS_rt_sigprocmask, how, (long)set, (long)old, _NSIG / 8));
}
