#include <sys/resource.h>

#include "internal/syscall.h"

int getrlimit(int resource, struct rlimit *limits) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_getrlimit, resource, (long)limits));
}
