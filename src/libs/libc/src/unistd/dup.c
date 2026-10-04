#include <unistd.h>

#include "internal/syscall.h"

int dup(int oldfd) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_dup, oldfd));
}
