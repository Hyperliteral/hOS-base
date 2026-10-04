#include <time.h>

time_t time(time_t *t) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
    return (time_t)-1;
  if (t)
    *t = ts.tv_sec;
  return ts.tv_sec;
}
