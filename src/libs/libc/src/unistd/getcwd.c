#include <unistd.h>

#include "internal/syscall.h"

char *getcwd(char *buf, size_t size) {
  // The kernel reports ERANGE for short buffers through the usual path.
  if (__cavos_ret((unsigned long)__syscall2(SYS_getcwd, (long)buf,
                                           (long)size)) < 0)
    return 0;
  return buf;
}
