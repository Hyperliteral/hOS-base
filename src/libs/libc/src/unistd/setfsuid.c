#include <sys/fsuid.h>
#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

// No kernel setfsuid yet: ENOSYS at runtime, linkable today.
int setfsuid(uid_t uid) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_setfsuid, uid));
}
