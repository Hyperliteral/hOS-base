#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "internal/syscall.h"

int dup2(int oldfd, int newfd) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_dup2, oldfd, newfd));
}
