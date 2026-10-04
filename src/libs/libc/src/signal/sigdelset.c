#include <errno.h>
#include <signal.h>

static int valid(int sig) { return sig > 0 && sig < _NSIG; }

int sigdelset(sigset_t *set, int sig) {
  if (!set || !valid(sig)) {
    *__errno_location() = EINVAL;
    return -1;
  }
  unsigned bit = (unsigned)(sig - 1);
  set->__bits[bit / (8 * sizeof(unsigned long))] &=
      ~(1UL << (bit % (8 * sizeof(unsigned long))));
  return 0;
}
