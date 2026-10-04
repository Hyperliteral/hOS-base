#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>

#include "internal/dirent_impl.h"
#include "internal/syscall.h"

DIR *opendir(const char *path) {
  int fd = (int)__cavos_ret((unsigned long)__syscall4(
      SYS_openat, AT_FDCWD, (long)path, O_RDONLY | O_DIRECTORY, (long)0));
  if (fd < 0)
    return 0;
  CavosDir *dir = malloc(sizeof(*dir));
  if (!dir) {
    __syscall1(SYS_close, fd);
    return 0;
  }
  dir->fd = fd;
  dir->chunk_len = 0;
  dir->chunk_pos = 0;
  return (DIR *)dir;
}
