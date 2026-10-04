#include <errno.h>
#include <sys/stat.h>

// No kernel mknod: ENOSYS at runtime, linkable today.
int mknod(const char *path, mode_t mode, dev_t dev) {
  (void)path;
  (void)mode;
  (void)dev;
  *__errno_location() = ENOSYS;
  return -1;
}
