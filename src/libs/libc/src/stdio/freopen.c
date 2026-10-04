// freopen reuses the caller's FILE object in place: the old fd is closed
// (but the FILE itself is never freed, so a F_PERM stdio stream survives the
// call as C requires) and the struct is reset around the newly opened fd.
// A NULL stream behaves like fopen. On open failure the old stream is
// dissolved (fd set to -1) and NULL is returned, per C11 7.21.5.4.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

FILE *freopen(const char *restrict path, const char *restrict mode,
              FILE *restrict file) {
  if (!file)
    return fopen(path, mode);
  int want = __cavos_parse_mode(mode);
  if (want < 0 || !path) {
    *__errno_location() = EINVAL;
    return 0;
  }
  int fd = (int)__cavos_ret((unsigned long)__syscall4(
      SYS_openat, AT_FDCWD, (long)path, want, (long)0666));
  if (fd < 0) {
    if (file->fd >= 0 &&
        __cavos_ret((unsigned long)__syscall1(SYS_close, file->fd)) == 0)
      file->fd = -1;
    __cavos_pushback_drop(file);
    return 0;
  }
  if (file->fd >= 0 && file->fd != fd)
    __syscall1(SYS_close, file->fd);
  __cavos_pushback_drop(file);
  memset(file, 0, sizeof(*file));
  file->fd = fd;
  switch (want & O_ACCMODE) {
  case O_RDONLY: file->flags = CAVOS_F_NOWR; break;
  case O_WRONLY: file->flags = CAVOS_F_NORD; break;
  default: file->flags = 0; break;
  }
  if (want & O_APPEND)
    file->flags |= CAVOS_F_APP;
  return file;
}
