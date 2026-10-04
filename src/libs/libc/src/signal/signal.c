// BSD signal() over sigaction: handler installed with SA_RESTART and an
// empty mask, previous handler returned (SIG_ERR on failure).
#include <signal.h>

void (*signal(int sig, void (*func)(int)))(int) {
  struct sigaction sa;
  struct sigaction old;
  sa.sa_handler = func;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(sig, &sa, &old) < 0)
    return SIG_ERR;
  return old.sa_handler;
}
