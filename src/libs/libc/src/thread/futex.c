// Futex primitives + abstime helper. The kernel's FUTEX_WAIT takes a
// *relative* timeout (ticks); CLOCK_REALTIME is refused outright, so timed
// waits convert with clock_gettime(CLOCK_REALTIME) here in userspace.
#include <errno.h>
#include <time.h>

#include "internal/pthread_impl.h"
#include "internal/syscall.h"

long __cavos_futex(long *addr, int op, int val,
                   const struct timespec *timeout) {
  return __cavos_ret((unsigned long)__syscall6(
      SYS_futex, (long)addr, op, val, (long)timeout, 0, 0));
}

int __cavos_abstime_to_rel(const struct timespec *abstime,
                           struct timespec *rel) {
  struct timespec now;
  if (!abstime || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
    return EINVAL;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0)
    return EINVAL;
  long sec = abstime->tv_sec - now.tv_sec;
  long nsec = abstime->tv_nsec - now.tv_nsec;
  if (nsec < 0) {
    sec--;
    nsec += 1000000000L;
  }
  if (sec < 0) {
    rel->tv_sec = 0;
    rel->tv_nsec = 0;
  } else {
    rel->tv_sec = sec;
    rel->tv_nsec = nsec;
  }
  return 0;
}
