#include <errno.h>
#include <sys/socket.h>

// The cavOS kernel has no SYS_setsockopt behind its Linux-compatible table
// (getsockopt exists, setsockopt does not). Link stub: fail loudly so
// callers surface the gap instead of silently running with defaults.
int setsockopt(int fd, int level, int optname, const void *optval,
               socklen_t optlen) {
  (void)fd;
  (void)level;
  (void)optname;
  (void)optval;
  (void)optlen;
  *__errno_location() = ENOSYS;
  return -1;
}
