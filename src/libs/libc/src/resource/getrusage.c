#include <sys/resource.h>

#include "internal/syscall.h"

// Same story as setrlimit: provided for linking, ENOSYS at runtime until the
// kernel implements process accounting.
int getrusage(int who, struct rusage *usage) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_getrusage, who, (long)usage));
}
