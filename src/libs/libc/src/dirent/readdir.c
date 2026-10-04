#include <dirent.h>
#include <stdint.h>
#include <string.h>

#include "internal/dirent_impl.h"
#include "internal/syscall.h"

struct dirent *readdir(DIR *dirp) {
  if (!dirp)
    return 0;
  CavosDir *dir = (CavosDir *)dirp;
  if (dir->fd < 0)
    return 0;
  if (dir->chunk_pos >= dir->chunk_len) {
    long got = __cavos_ret((unsigned long)__syscall3(
        SYS_getdents64, dir->fd, (long)dir->chunk, sizeof(dir->chunk)));
    if (got <= 0)
      return 0; // 0: end of directory, -1: error with errno set
    dir->chunk_len = (size_t)got;
    dir->chunk_pos = 0;
  }
  struct cavos_linux_dirent64 *record =
      (struct cavos_linux_dirent64 *)(dir->chunk + dir->chunk_pos);
  if (record->d_reclen == 0 ||
      dir->chunk_pos + record->d_reclen > dir->chunk_len)
    return 0; // corrupt record: stop rather than overrun
  struct dirent *entry = &dir->storage.entry;
  entry->d_ino = (ino_t)record->d_ino;
  entry->d_off = (off_t)record->d_off;
  entry->d_reclen = (reclen_t)record->d_reclen;
  entry->d_type = record->d_type;
  size_t name_max = sizeof(dir->storage.space) -
                    __builtin_offsetof(struct dirent, d_name) - 1;
  strncpy(entry->d_name, record->d_name, name_max);
  entry->d_name[name_max] = '\0';
  dir->chunk_pos += record->d_reclen;
  return entry;
}
