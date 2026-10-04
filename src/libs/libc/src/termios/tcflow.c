#include <sys/ioctl.h>
#include <termios.h>

#include "internal/syscall.h"

int tcflow(int fd, int action) {
  return (int)__cavos_ret(
      (unsigned long)__syscall3(SYS_ioctl, fd, TCXONC, (long)action));
}
