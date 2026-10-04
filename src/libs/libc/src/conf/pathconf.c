#include <errno.h>
#include <limits.h>
#include <unistd.h>

// Filesystem queries without a statvfs path: answer the constants every
// real filesystem here satisfies, EINVAL for the rest (like musl for
// unknown names).
long pathconf(const char *path, int name) {
  (void)path;
  switch (name) {
  case _PC_PATH_MAX:
    return 4096;
  case _PC_NAME_MAX:
    return 255;
  case _PC_PIPE_BUF:
    return 4096;
  case _PC_CHOWN_RESTRICTED:
    return 1;
  case _PC_NO_TRUNC:
    return 1;
  case _PC_VDISABLE:
    return 0;
  default:
    *__errno_location() = EINVAL;
    return -1;
  }
}
