#include <signal.h>
#include <sys/types.h>

#include "internal/syscall.h"

int kill(pid_t pid, int sig) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_kill, pid, sig));
}
