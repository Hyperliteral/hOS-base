#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

pid_t getpgid(pid_t pid) {
  return (pid_t)__cavos_ret((unsigned long)__syscall1(SYS_getpgid, pid));
}
