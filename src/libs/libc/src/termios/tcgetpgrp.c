#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "internal/syscall.h"

pid_t tcgetpgrp(int fd) {
  pid_t pgrp = -1;
  if (__cavos_ret((unsigned long)__syscall3(SYS_ioctl, fd, TIOCGPGRP,
                                           (long)&pgrp)) < 0)
    return -1;
  return pgrp;
}
