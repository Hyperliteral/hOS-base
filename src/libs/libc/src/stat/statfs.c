#include <errno.h>
#include <sys/statfs.h>

// No kernel statfs: ENOSYS at runtime, linkable today.
int statfs(const char *path, struct statfs *buf) {
  (void)path;
  (void)buf;
  *__errno_location() = ENOSYS;
  return -1;
}
