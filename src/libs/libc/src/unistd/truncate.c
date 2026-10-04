#include <errno.h>
#include <unistd.h>

// No kernel truncate: ENOSYS at runtime, linkable today.
int truncate(const char *path, off_t length) {
  (void)path;
  (void)length;
  *__errno_location() = ENOSYS;
  return -1;
}
