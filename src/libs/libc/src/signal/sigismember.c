#include <errno.h>
#include <signal.h>

static int valid(int sig) { return sig > 0 && sig < _NSIG; }

int sigismember(const sigset_t *set, int sig) {
  if (!set || !valid(sig)) {
    *__errno_location() = EINVAL;
    return -1;
  }
  unsigned bit = (unsigned)(sig - 1);
  return (set->__bits[bit / (8 * sizeof(unsigned long))] >>
          (bit % (8 * sizeof(unsigned long)))) &
         1;
}
