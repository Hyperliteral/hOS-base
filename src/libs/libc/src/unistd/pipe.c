#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "internal/syscall.h"

int pipe(int fd[2]) {
  return (int)__cavos_ret(
      (unsigned long)__syscall1(SYS_pipe, (long)fd));
}
