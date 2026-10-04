#include <errno.h>
#include <sys/socket.h>
#include <time.h>

// The cavOS kernel has no SYS_recvmmsg behind its Linux-compatible table.
// Link stub: fail loudly (ENOSYS) instead of leaving the symbol undefined.
int recvmmsg(int fd, struct mmsghdr *msgs, unsigned int count,
             unsigned int flags, struct timespec *timeout) {
  (void)fd;
  (void)msgs;
  (void)count;
  (void)flags;
  (void)timeout;
  *__errno_location() = ENOSYS;
  return -1;
}
