#include <errno.h>
#include <fcntl.h>

// No kernel fallocate: ENOSYS at runtime, linkable today.
// NOTE: POSIX specifies posix_fallocate returns the error number instead of
// setting errno; this wrapper follows the repo-wide -1 + errno convention.
int posix_fallocate(int fd, off_t offset, off_t len) {
  (void)fd;
  (void)offset;
  (void)len;
  *__errno_location() = ENOSYS;
  return ENOSYS;
}
