#include <sys/resource.h>

#include "internal/syscall.h"

// The kernel currently implements only the get half; until it grows the set
// half this fails cleanly with ENOSYS instead of at link time.
int setrlimit(int resource, const struct rlimit *limits) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_setrlimit, resource, (long)limits));
}
