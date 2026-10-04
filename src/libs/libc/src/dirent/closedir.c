#include <errno.h>
#include <dirent.h>
#include <stdlib.h>

#include "internal/dirent_impl.h"
#include "internal/syscall.h"

int closedir(DIR *dirp) {
  if (!dirp) {
    *__errno_location() = EINVAL;
    return -1;
  }
  CavosDir *dir = (CavosDir *)dirp;
  int result = 0;
  if (dir->fd >= 0)
    result =
        (int)__cavos_ret((unsigned long)__syscall1(SYS_close, dir->fd));
  free(dir);
  return result;
}
