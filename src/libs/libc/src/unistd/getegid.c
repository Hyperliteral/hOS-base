#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

gid_t getegid(void) {
  return (gid_t)__cavos_ret((unsigned long)__syscall0(SYS_getegid));
}
