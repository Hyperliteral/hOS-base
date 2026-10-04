#include <sys/socket.h>

#include "internal/syscall.h"

int socket(int domain, int type, int protocol) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_socket, domain, type,
                                                   protocol));
}
