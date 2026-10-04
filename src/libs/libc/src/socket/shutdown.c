#include <errno.h>
#include <sys/socket.h>

// The cavOS kernel has no SYS_shutdown behind its Linux-compatible table.
// Link stub: fail loudly (ENOSYS) instead of leaving the symbol undefined.
int shutdown(int fd, int how) {
  (void)fd;
  (void)how;
  *__errno_location() = ENOSYS;
  return -1;
}
