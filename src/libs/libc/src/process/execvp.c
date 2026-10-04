#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// __environ is musl-private; declared through the internal header.
#include "internal/impl.h"

// PATH-searching exec. Absolute/relative names (containing '/') go straight
// to execve; bare names are tried against each PATH component, defaulting to
// the classic confstr path when PATH is unset. Returns on failure with errno
// from the last attempt (ENOENT when nothing was even tried).
int execvp(const char *file, char *const argv[]) {
  if (!file || !argv) {
    *__errno_location() = EINVAL;
    return -1;
  }
  if (strchr(file, '/'))
    return execve(file, argv, __environ);
  const char *path = getenv("PATH");
  if (!path || !*path)
    path = "/bin:/usr/bin";
  char candidate[512];
  *__errno_location() = ENOENT;
  for (const char *dir = path;;) {
    size_t dir_len = strcspn(dir, ":");
    if (dir_len + 1 + strlen(file) < sizeof(candidate)) {
      memcpy(candidate, dir, dir_len);
      candidate[dir_len] = '/';
      strcpy(candidate + dir_len + 1, file);
      execve(candidate, argv, __environ);
      if (*__errno_location() != ENOENT)
        return -1;
    }
    dir += dir_len;
    if (!*dir)
      break;
    dir++;
  }
  return -1;
}
