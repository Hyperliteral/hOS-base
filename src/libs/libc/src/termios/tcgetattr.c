#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "internal/syscall.h"

int tcgetattr(int fd, struct termios *term) {
  if (!term) {
    *__errno_location() = EINVAL;
    return -1;
  }
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_ioctl, fd, TCGETS, (long)term));
}
