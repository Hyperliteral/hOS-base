#include <errno.h>
#include <sys/statvfs.h>

// No kernel fstatvfs: ENOSYS at runtime, linkable today.
int fstatvfs(int fd, struct statvfs *buf) {
  (void)fd;
  (void)buf;
  *__errno_location() = ENOSYS;
  return -1;
}
