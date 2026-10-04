#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

// Wraps an existing fd without taking ownership questions: the mode must
// allow the fd's access, verified cheaply. Unbuffered like everything here.
FILE *fdopen(int fd, const char *mode) {
  int want = __cavos_parse_mode(mode);
  if (want < 0 || fd < 0) {
    *__errno_location() = EINVAL;
    return 0;
  }
  // The mode must allow what the fd was opened for; ask the kernel rather
  // than trusting the caller. Unverifiable (fcntl failed) means permissive.
  int got = fcntl(fd, F_GETFL);
  if (got >= 0) {
    int want_acc = want & O_ACCMODE;
    int got_acc = got & O_ACCMODE;
    if (want_acc == O_RDONLY && got_acc == O_WRONLY)
      goto bad;
    if (want_acc == O_WRONLY && got_acc == O_RDONLY)
      goto bad;
  }
  FILE *file = malloc(sizeof(*file));
  if (!file)
    return 0;
  memset(file, 0, sizeof(*file));
  file->fd = fd;
  switch (want & O_ACCMODE) {
  case O_RDONLY: file->flags = CAVOS_F_NOWR; break; // read-only: no write
  case O_WRONLY: file->flags = CAVOS_F_NORD; break; // write-only: no read
  default: file->flags = 0; break;
  }
  if (want & O_APPEND)
    file->flags |= CAVOS_F_APP;
  return file;
bad:
  *__errno_location() = EINVAL;
  return 0;
}
