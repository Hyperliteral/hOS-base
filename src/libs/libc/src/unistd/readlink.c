#include <unistd.h>

#include "internal/syscall.h"

ssize_t readlink(const char *restrict path, char *restrict buf, size_t size) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall3(SYS_readlink,
                                                      (long)path, (long)buf,
                                                      (long)size));
}
