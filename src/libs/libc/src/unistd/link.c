#include <unistd.h>

#include "internal/syscall.h"

int link(const char *oldpath, const char *newpath) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_link, (long)oldpath, (long)newpath));
}
