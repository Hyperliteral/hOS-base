#include <unistd.h>

#include "internal/syscall.h"

pid_t setsid(void) {
  return (pid_t)__cavos_ret((unsigned long)__syscall0(SYS_setsid));
}
