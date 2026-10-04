#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

// No kernel setresgid yet: ENOSYS at runtime, linkable today.
int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_setresgid, rgid, egid, sgid));
}
