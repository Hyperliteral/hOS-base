#include <sys/time.h>
#include <time.h>

#include "internal/syscall.h"

// No kernel gettimeofday: CLOCK_REALTIME is the same clock, microseconds
// derived from nanoseconds. The timezone argument is obsolete (and ignored,
// like musl ignores it).
int gettimeofday(struct timeval *restrict tv, void *restrict tz) {
  (void)tz;
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
    return -1;
  tv->tv_sec = ts.tv_sec;
  tv->tv_usec = (suseconds_t)(ts.tv_nsec / 1000);
  return 0;
}
