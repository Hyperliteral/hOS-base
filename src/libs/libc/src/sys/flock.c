#include <sys/file.h>

#include "internal/syscall.h"

int flock(int fd, int op) {
  return (int)__cavos_ret((unsigned long)__syscall2(SYS_flock, fd, op));
}
