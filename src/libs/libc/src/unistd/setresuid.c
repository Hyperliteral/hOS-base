#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

// No kernel setresuid yet: ENOSYS at runtime, linkable today.
int setresuid(uid_t ruid, uid_t euid, uid_t suid) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_setresuid, ruid, euid, suid));
}
