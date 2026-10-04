#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

int setgid(gid_t gid) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_setgid, gid));
}
