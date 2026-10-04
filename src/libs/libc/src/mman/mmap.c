#include <errno.h>
#include <sys/mman.h>

#include "internal/syscall.h"

// Protection and flag constants come from the vendored <sys/mman.h> and
// already match the kernel's Linux-compatible table. Errors surface as
// MAP_FAILED with errno set, per musl.
void *mmap(void *address, size_t length, int protection, int flags, int fd,
           off_t offset) {
  unsigned long result = (unsigned long)__syscall6(
      SYS_mmap, (long)address, (long)length, protection, flags, fd,
      (long)offset);
  if (result > -4096UL) {
    *__errno_location() = (int)-(long)result;
    return MAP_FAILED;
  }
  return (void *)result;
}
