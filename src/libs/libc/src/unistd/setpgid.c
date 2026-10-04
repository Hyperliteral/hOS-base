#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

int setpgid(pid_t pid, pid_t pgid) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_setpgid, pid, pgid));
}
