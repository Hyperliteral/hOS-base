#include <signal.h>

// No separate pthread mask state exists (no threads layer here): the
// process mask is the thread mask, so forward directly.
int pthread_sigmask(int how, const sigset_t *restrict set,
                    sigset_t *restrict old) {
  return sigprocmask(how, set, old);
}
