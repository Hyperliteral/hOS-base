#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "internal/syscall.h"

pid_t waitpid(pid_t pid, int *status, int options) {
  return (pid_t)__cavos_ret((unsigned long)__syscall4(
      SYS_wait4, pid, (long)status, options, (long)0));
}
