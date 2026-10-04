#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

int __cavos_parse_mode(const char *mode) {
  if (!mode || !mode[0])
    return -1;
  int access = -1;
  int flags = 0;
  int plus = 0;
  for (const char *p = mode; *p; p++) {
    switch (*p) {
    case 'r': access = O_RDONLY; break;
    case 'w': access = O_WRONLY; flags |= O_CREAT | O_TRUNC; break;
    case 'a': access = O_WRONLY; flags |= O_CREAT | O_APPEND; break;
    case '+': plus = 1; break;
    case 'b': break; // binary hint, meaningless here
    default: return -1;
    }
  }
  if (access < 0)
    return -1;
  if (plus)
    access = O_RDWR;
  return access | flags;
}

FILE *fopen(const char *path, const char *mode) {
  int flags = __cavos_parse_mode(mode);
  if (flags < 0) {
    *__errno_location() = EINVAL;
    return 0;
  }
  int fd =
      (int)__cavos_ret((unsigned long)__syscall4(SYS_openat, AT_FDCWD,
                                                 (long)path, flags,
                                                 (long)0666));
  if (fd < 0)
    return 0;
  FILE *file = malloc(sizeof(*file));
  if (!file) {
    __syscall1(SYS_close, fd);
    return 0;
  }
  memset(file, 0, sizeof(*file));
  file->fd = fd;
  // musl flag semantics: readable unless F_NOWR, writable unless F_NORD.
  switch (flags & O_ACCMODE) {
  case O_RDONLY: file->flags = CAVOS_F_NOWR; break; // read-only: no write
  case O_WRONLY: file->flags = CAVOS_F_NORD; break; // write-only: no read
  default: file->flags = 0; break;
  }
  if (flags & O_APPEND)
    file->flags |= CAVOS_F_APP;
  return file;
}
