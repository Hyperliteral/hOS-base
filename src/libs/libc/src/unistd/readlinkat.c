#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// No kernel readlinkat: AT_FDCWD paths forward to readlink, else ENOSYS.
ssize_t readlinkat(int dirfd, const char *restrict path, char *restrict buf,
                   size_t size) {
  if (dirfd == AT_FDCWD) return readlink(path, buf, size);
  *__errno_location() = ENOSYS;
  return -1;
}
