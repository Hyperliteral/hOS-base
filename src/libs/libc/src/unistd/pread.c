#include <unistd.h>

#include "internal/syscall.h"

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
  return (ssize_t)__cavos_ret((unsigned long)__syscall4(SYS_pread64, fd,
                                                       (long)buf, (long)count,
                                                       (long)offset));
}
