#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

int getgroups(int count, gid_t list[]) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_getgroups, count, (long)list));
}
