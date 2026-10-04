#include <unistd.h>

#include "internal/syscall.h"

int fchdir(int fd) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_fchdir, fd));
}
