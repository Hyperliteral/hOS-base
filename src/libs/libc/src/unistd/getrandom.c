#include <sys/random.h>
#include <unistd.h>

#include "internal/syscall.h"

ssize_t getrandom(void *buffer, size_t length, unsigned flags) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall3(
      SYS_getrandom, (long)buffer, (long)length, flags));
}
