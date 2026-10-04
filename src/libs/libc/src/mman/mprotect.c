#include <sys/mman.h>

#include "internal/syscall.h"

int mprotect(void *address, size_t length, int protection) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_mprotect, (long)address,
                                                   (long)length, protection));
}
