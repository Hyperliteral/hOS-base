#include <fcntl.h>

// No kernel fadvise: advice is a hint, so it is accepted and ignored.
// NOTE: POSIX specifies posix_fadvise returns the error number; success here
// means return 0 with errno untouched.
int posix_fadvise(int fd, off_t offset, off_t len, int advice) {
  (void)fd;
  (void)offset;
  (void)len;
  (void)advice;
  return 0;
}
