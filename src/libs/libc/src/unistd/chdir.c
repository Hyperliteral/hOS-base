#include <unistd.h>

#include "internal/syscall.h"

int chdir(const char *path) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_chdir, (long)path));
}
