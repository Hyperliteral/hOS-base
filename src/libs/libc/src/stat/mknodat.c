#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

// No kernel mknodat: AT_FDCWD paths forward to mknod, anything else is ENOSYS.
int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev) {
  if (dirfd == AT_FDCWD) return mknod(path, mode, dev);
  *__errno_location() = ENOSYS;
  return -1;
}
