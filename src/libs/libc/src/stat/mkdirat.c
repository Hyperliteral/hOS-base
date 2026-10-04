#include <sys/stat.h>

#include "internal/syscall.h"

int mkdirat(int dirfd, const char *path, mode_t mode) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_mkdirat, dirfd,
                                                   (long)path, (long)mode));
}
