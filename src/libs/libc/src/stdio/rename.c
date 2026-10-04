#include <stdio.h>

#include "internal/syscall.h"

int rename(const char *oldpath, const char *newpath) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_rename, (long)oldpath, (long)newpath));
}
