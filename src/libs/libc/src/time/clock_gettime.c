#include <time.h>

#include "internal/syscall.h"

int clock_gettime(clockid_t clock, struct timespec *tp) {
  return (int)__cavos_ret((unsigned long)__syscall2(SYS_clock_gettime, clock,
                                                   (long)tp));
}
