#include <sys/ioctl.h>
#include <termios.h>

#include "internal/syscall.h"

int tcflush(int fd, int queue) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_ioctl, fd, TCFLSH, (long)queue));
}
