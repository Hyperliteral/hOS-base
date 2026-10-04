#include <sys/time.h>

#include "internal/syscall.h"

int setitimer(int which, const struct itimerval *restrict new,
              struct itimerval *restrict old) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_setitimer, which,
                                                   (long)new, (long)old));
}
