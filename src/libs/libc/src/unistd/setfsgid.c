#include <sys/fsuid.h>
#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

// No kernel setfsgid yet: ENOSYS at runtime, linkable today.
int setfsgid(gid_t gid) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_setfsgid, gid));
}
