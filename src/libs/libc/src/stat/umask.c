#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

mode_t umask(mode_t mask) {
  return (mode_t)__cavos_ret((unsigned long)__syscall1(SYS_umask, mask));
}
