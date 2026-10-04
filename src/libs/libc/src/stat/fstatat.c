#include <sys/stat.h>

#include "internal/syscall.h"

int fstatat(int dirfd, const char *restrict path, struct stat *restrict buf,
            int flags) {
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_newfstatat, dirfd,
                                                   (long)path, (long)buf,
                                                   flags));
}
