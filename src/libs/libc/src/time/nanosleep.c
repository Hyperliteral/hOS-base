#include <time.h>

#include "internal/syscall.h"

int nanosleep(const struct timespec *req, struct timespec *rem) {
  return (int)__cavos_ret((unsigned long)__syscall2(SYS_nanosleep, (long)req,
                                                   (long)rem));
}
