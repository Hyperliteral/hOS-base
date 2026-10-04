#include <unistd.h>

#include "internal/syscall.h"

int pipe2(int fd[2], int flags) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_pipe2, (long)fd, flags));
}
