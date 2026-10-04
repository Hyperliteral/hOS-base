#include <unistd.h>

#include "internal/syscall.h"

int chroot(const char *path) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_chroot, (long)path));
}
