#include <unistd.h>

#include "internal/syscall.h"

// No kernel rmdir yet: ENOSYS at runtime, linkable today.
int rmdir(const char *path) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_rmdir, (long)path));
}
