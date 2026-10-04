#include <errno.h>
#include <sys/stat.h>

// No kernel futimens: ENOSYS at runtime, linkable today.
int futimens(int fd, const struct timespec times[2]) {
  (void)fd;
  (void)times;
  *__errno_location() = ENOSYS;
  return -1;
}
