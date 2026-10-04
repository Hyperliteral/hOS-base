#include <errno.h>
#include <sys/mount.h>

// No kernel umount2: ENOSYS at runtime, linkable today.
int umount(const char *target) {
  (void)target;
  *__errno_location() = ENOSYS;
  return -1;
}

int umount2(const char *target, int flags) {
  (void)target;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}
