#include <fcntl.h>
#include <unistd.h>

#include "internal/syscall.h"

// Effective-ID access check. No setuid complexity applies here (single-user
// embedded target), so this forwards to faccessat with AT_EACCESS.
int eaccess(const char *path, int mode) {
  return faccessat(AT_FDCWD, path, mode, AT_EACCESS);
}
