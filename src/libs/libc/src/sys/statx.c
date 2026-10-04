#include <sys/stat.h>

#include "internal/syscall.h"

int statx(int dirfd, const char *restrict path, int flags, unsigned mask,
          struct statx *restrict buf) {
  return (int)__cavos_ret((unsigned long)__syscall5(SYS_statx, dirfd,
                                                   (long)path, flags, mask,
                                                   (long)buf));
}
