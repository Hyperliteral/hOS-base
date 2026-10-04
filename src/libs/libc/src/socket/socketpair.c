#include <sys/socket.h>

#include "internal/syscall.h"

int socketpair(int domain, int type, int protocol, int fd[2]) {
  return (int)__cavos_ret((unsigned long)__syscall4(SYS_socketpair, domain,
                                                   type, protocol, (long)fd));
}
