#include <errno.h>
#include <unistd.h>

// No kernel fdatasync: ENOSYS at runtime, linkable today.
int fdatasync(int fd) {
  (void)fd;
  *__errno_location() = ENOSYS;
  return -1;
}
