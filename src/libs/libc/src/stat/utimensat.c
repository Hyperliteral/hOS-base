#include <errno.h>
#include <sys/stat.h>

// No kernel utimensat: ENOSYS at runtime, linkable today.
int utimensat(int dirfd, const char *path, const struct timespec times[2],
              int flags) {
  (void)dirfd;
  (void)path;
  (void)times;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}
