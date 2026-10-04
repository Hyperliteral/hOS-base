#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

int setuid(uid_t uid) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_setuid, uid));
}
