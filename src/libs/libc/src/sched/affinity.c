#include <errno.h>
#include <sched.h>
#include <sys/types.h>

#include "internal/syscall.h"

// Single scheduling class: reading the affinity mask is a passthrough,
// setting it has nothing to apply to, so it reports ENOSYS.
int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *mask) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_sched_getaffinity,
                                                   pid, size, (long)mask));
}

int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *mask) {
  (void)pid;
  (void)size;
  (void)mask;
  *__errno_location() = ENOSYS;
  return -1;
}

int __sched_cpucount(size_t size, const cpu_set_t *set) {
  const unsigned char *p = (const unsigned char *)set;
  int                  count = 0;
  for (size_t i = 0; i < size; i++) {
    unsigned char v = p[i];
    while (v) {
      count += v & 1;
      v >>= 1;
    }
  }
  return count;
}
