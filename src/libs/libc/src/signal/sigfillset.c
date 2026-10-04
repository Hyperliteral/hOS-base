#include <signal.h>

int sigfillset(sigset_t *set) {
  if (sigemptyset(set) < 0)
    return -1;
  // All real-time and standard signals below _NSIG.
  for (int sig = 1; sig < _NSIG; sig++)
    sigaddset(set, sig);
  return 0;
}
