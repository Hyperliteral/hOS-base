#include <errno.h>
#include <sys/mman.h>

// No kernel madvise: ENOSYS at runtime, linkable today.
// NOTE: POSIX specifies posix_madvise returns the error number instead of
// setting errno; this wrapper follows the repo-wide -1 + errno convention.
int posix_madvise(void *addr, size_t len, int advice) {
  (void)addr;
  (void)len;
  (void)advice;
  *__errno_location() = ENOSYS;
  return ENOSYS;
}
