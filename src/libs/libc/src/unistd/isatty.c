#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "internal/syscall.h"

// A tty answers TCGETS; anything else makes the kernel fail the ioctl, which
// is exactly the contract musl documents (0 with errno set, ENOTTY for
// non-terminals, EBADF for bad fds - all produced by __cavos_ret).
int isatty(int fd) {
  struct termios term;
  long result = __cavos_ret(
      (unsigned long)__syscall3(SYS_ioctl, fd, TCGETS, (long)&term));
  return result == 0;
}
