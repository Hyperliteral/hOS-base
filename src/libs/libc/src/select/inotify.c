#include <errno.h>
#include <stdint.h>
#include <sys/inotify.h>

#include "internal/syscall.h"

int inotify_init(void) {
  return (int)__cavos_ret(
      (unsigned long)__syscall0(SYS_inotify_init));
}

int inotify_init1(int flags) {
  // Only a flagless init maps to inotify_init; anything else the kernel
  // cannot express (no IN_CLOEXEC/IN_NONBLOCK support), so reject it.
  if (flags != 0) {
    *__errno_location() = EINVAL;
    return -1;
  }
  return inotify_init();
}

int inotify_add_watch(int fd, const char *path, uint32_t mask) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_inotify_add_watch, fd,
                                                   (long)path, mask));
}

int inotify_rm_watch(int fd, int wd) {
  return (int)__cavos_ret(
      (unsigned long)__syscall2(SYS_inotify_rm_watch, fd, wd));
}
