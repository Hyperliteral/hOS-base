#include <errno.h>
#include <time.h>
#include <unistd.h>

// usleep over nanosleep: the only sleeping primitive this libc owns. Rounds
// up sub-tick values like musl does; EINTR propagates (no restart loop -
// the caller owns that policy, matching musl).
int usleep(useconds_t duration) {
  struct timespec ts = {.tv_sec = duration / 1000000,
                        .tv_nsec = (duration % 1000000) * 1000};
  if (nanosleep(&ts, &ts) != 0)
    return -1;
  return 0;
}
