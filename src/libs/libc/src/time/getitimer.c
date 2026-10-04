#include <sys/time.h>

#include "internal/syscall.h"

// No kernel getitimer: ENOSYS at runtime, linkable today.
int getitimer(int which, struct itimerval *value) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_getitimer, which, (long)value));
}
