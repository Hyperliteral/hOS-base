#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

pid_t getppid(void) {
  return (pid_t)__cavos_ret((unsigned long)__syscall0(SYS_getppid));
}
