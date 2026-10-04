#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

// No SYS_getpgrp on Linux: a process group query is getpgid(0).
pid_t getpgrp(void) {
  return (pid_t)__cavos_ret((unsigned long)__syscall1(SYS_getpgid, 0));
}
