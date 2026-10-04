#include <sys/socket.h>

#include "internal/syscall.h"

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall3(SYS_sendmsg, fd,
                                                       (long)msg, flags));
}
