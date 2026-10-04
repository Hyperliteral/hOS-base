#include <unistd.h>

#include "internal/syscall.h"

int unlink(const char *path) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_unlink, (long)path));
}
