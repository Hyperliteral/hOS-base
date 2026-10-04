#include <sys/stat.h>
#include <sys/types.h>

#include "internal/syscall.h"

int chown(const char *path, uid_t owner, gid_t group) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_chown, (long)path,
                                                   owner, group));
}
