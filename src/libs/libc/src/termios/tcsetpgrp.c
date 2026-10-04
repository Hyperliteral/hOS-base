#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "internal/syscall.h"

int tcsetpgrp(int fd, pid_t pgrp) {
  // Returns 0 on success like the ioctl path does; musl returns 0 too.
  if (__cavos_ret((unsigned long)__syscall3(SYS_ioctl, fd, TIOCSPGRP,
                                           (long)&pgrp)) < 0)
    return -1;
  return 0;
}
