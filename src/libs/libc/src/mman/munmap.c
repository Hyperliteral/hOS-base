#include <sys/mman.h>

#include "internal/syscall.h"

int munmap(void *address, size_t length) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_munmap, (long)address, (long)length));
}
