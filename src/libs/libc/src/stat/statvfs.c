#include <errno.h>
#include <sys/statvfs.h>

// No kernel statvfs: ENOSYS at runtime, linkable today.
int statvfs(const char *restrict path, struct statvfs *restrict buf) {
  (void)path;
  (void)buf;
  *__errno_location() = ENOSYS;
  return -1;
}
