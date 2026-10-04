#include <unistd.h>

#include "internal/syscall.h"

int close(int fd) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_close, fd));
}
