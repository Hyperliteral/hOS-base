#include <sys/time.h>
#include <unistd.h>

// One-shot SIGALRM via the interval timer; returns the previous pending
// seconds like musl does (rounded up from the old timer, if any).
unsigned alarm(unsigned seconds) {
  struct itimerval old;
  struct itimerval new;
  new.it_interval.tv_sec = 0;
  new.it_interval.tv_usec = 0;
  new.it_value.tv_sec = (time_t)seconds;
  new.it_value.tv_usec = 0;
  if (setitimer(ITIMER_REAL, &new, &old) < 0)
    return 0;
  if (old.it_value.tv_sec < 0)
    return 0;
  return (unsigned)old.it_value.tv_sec +
         (old.it_value.tv_usec > 0 ? 1 : 0);
}
