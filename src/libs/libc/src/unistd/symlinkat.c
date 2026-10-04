#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// No kernel symlinkat: AT_FDCWD paths forward to symlink, else ENOSYS.
int symlinkat(const char *target, int newdirfd, const char *linkpath) {
  if (newdirfd == AT_FDCWD) return symlink(target, linkpath);
  *__errno_location() = ENOSYS;
  return -1;
}
