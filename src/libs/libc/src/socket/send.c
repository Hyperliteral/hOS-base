#include <sys/socket.h>

#include "internal/syscall.h"

// No dedicated SYS_send: send is sendto with no destination address.
ssize_t send(int fd, const void *buf, size_t len, int flags) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall6(SYS_sendto, fd,
                                                       (long)buf, len, flags,
                                                       0, 0));
}
