#include <errno.h>
#include <signal.h>
#include <unistd.h>

int raise(int sig) {
  if (sig <= 0 || sig >= _NSIG) {
    *__errno_location() = EINVAL;
    return -1;
  }
  return kill(getpid(), sig);
}
