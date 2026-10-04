#include <errno.h>
#include <unistd.h>

// No kernel ftruncate: ENOSYS at runtime, linkable today.
int ftruncate(int fd, off_t length) {
  (void)fd;
  (void)length;
  *__errno_location() = ENOSYS;
  return -1;
}
