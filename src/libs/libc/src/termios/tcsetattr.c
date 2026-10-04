#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "internal/syscall.h"

int tcsetattr(int fd, int actions, const struct termios *term) {
  unsigned long request;
  if (!term) {
    *__errno_location() = EINVAL;
    return -1;
  }
  switch (actions) {
  case TCSANOW: request = TCSETS; break;
  case TCSADRAIN: request = TCSETSW; break;
  case TCSAFLUSH: request = TCSETSF; break;
  default:
    *__errno_location() = EINVAL;
    return -1;
  }
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_ioctl, fd, request, (long)term));
}
