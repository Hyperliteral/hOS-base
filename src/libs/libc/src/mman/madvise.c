#include <errno.h>
#include <sys/mman.h>

// No kernel madvise: ENOSYS at runtime, linkable today.
int madvise(void *addr, size_t len, int advice) {
  (void)addr;
  (void)len;
  (void)advice;
  *__errno_location() = ENOSYS;
  return -1;
}
