#include <fcntl.h>
#include <unistd.h>

#include "internal/syscall.h"

int faccessat(int dirfd, const char *path, int mode, int flags) {
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_faccessat, dirfd,
                                                   (long)path, mode, flags));
}
