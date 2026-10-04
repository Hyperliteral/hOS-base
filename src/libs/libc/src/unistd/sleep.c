#include <time.h>
#include <unistd.h>

// No signal delivery nuances apply (single-threaded, signals delivered
// synchronously by the kernel here): one nanosleep, remaining time reported
// through the same errno path as everything else.
unsigned sleep(unsigned seconds) {
  struct timespec req;
  struct timespec rem;
  req.tv_sec = seconds;
  req.tv_nsec = 0;
  if (nanosleep(&req, &rem) < 0)
    return (unsigned)rem.tv_sec + (rem.tv_nsec > 0 ? 1 : 0);
  return 0;
}
