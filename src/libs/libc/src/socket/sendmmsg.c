#include <errno.h>
#include <sys/socket.h>

// The cavOS kernel has no SYS_sendmmsg behind its Linux-compatible table.
// Link stub: fail loudly (ENOSYS) instead of leaving the symbol undefined.
int sendmmsg(int fd, struct mmsghdr *msgs, unsigned int count,
             unsigned int flags) {
  (void)fd;
  (void)msgs;
  (void)count;
  (void)flags;
  *__errno_location() = ENOSYS;
  return -1;
}
