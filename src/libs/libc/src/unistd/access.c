#include <unistd.h>

#include "internal/syscall.h"

int access(const char *path, int mode) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_access, (long)path, mode));
}
