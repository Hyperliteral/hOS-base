#include <sys/eventfd.h>

#include "internal/syscall.h"

int eventfd(unsigned int initval, int flags) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_eventfd2, initval, flags));
}
