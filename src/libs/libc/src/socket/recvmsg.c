#include <sys/socket.h>

#include "internal/syscall.h"

ssize_t recvmsg(int fd, struct msghdr *msg, int flags) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall3(SYS_recvmsg, fd,
                                                       (long)msg, flags));
}
