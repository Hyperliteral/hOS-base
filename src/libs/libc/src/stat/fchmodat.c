#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

// No kernel fchmodat: AT_FDCWD paths forward to chmod, anything else is ENOSYS.
int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
  if (dirfd == AT_FDCWD && flags == 0) return chmod(path, mode);
  *__errno_location() = ENOSYS;
  return -1;
}
