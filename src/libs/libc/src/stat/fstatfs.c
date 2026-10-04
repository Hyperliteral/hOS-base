#include <errno.h>
#include <sys/statfs.h>

// No kernel fstatfs: ENOSYS at runtime, linkable today.
int fstatfs(int fd, struct statfs *buf) {
  (void)fd;
  (void)buf;
  *__errno_location() = ENOSYS;
  return -1;
}
