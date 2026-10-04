#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "internal/syscall.h"

// unlink() for files, rmdir() for directories: try unlink first and fall
// back when the kernel reports a directory, mirroring musl's behavior.
int remove(const char *path) {
  if (unlink(path) == 0)
    return 0;
  if (*__errno_location() != EISDIR)
    return -1;
  return rmdir(path);
}
