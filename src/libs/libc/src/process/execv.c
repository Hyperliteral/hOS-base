// execv/execl/execlp. execv is execve with the process environment; execl
// collects varargs into an argv vector first. execlp reimplements the small
// PATH search from execvp.c (kept separate per layout: one file per family
// here) rather than calling across.
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal/impl.h"

int execv(const char *path, char *const argv[]) {
  return execve(path, argv, __environ);
}

static int path_exec(const char *file, char *const argv[]) {
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

int execl(const char *path, const char *arg0, ...) {
  if (!path) {
    *__errno_location() = EINVAL;
    return -1;
  }
  va_list ap;
  size_t count = 1;
  va_start(ap, arg0);
  for (const char *a = arg0; a; a = va_arg(ap, const char *))
    count++;
  va_end(ap);
  char **argv = malloc((count + 1) * sizeof(*argv));
  if (!argv)
    return -1;
  va_start(ap, arg0);
  for (size_t i = 0; i < count; i++)
    argv[i] = (char *)(i == 0 ? arg0 : va_arg(ap, const char *));
  // arg0 was consumed as argv[0] above without a va_arg call; refill the
  // rest starting from the second argument.
  va_end(ap);
  va_start(ap, arg0);
  (void)va_arg(ap, const char *);
  for (size_t i = 1; i < count; i++)
    argv[i] = va_arg(ap, char *);
  va_end(ap);
  argv[count] = 0;
  int r = execve(path, argv, __environ);
  int saved = *__errno_location();
  free(argv);
  *__errno_location() = saved;
  return r;
}

int execlp(const char *file, const char *arg0, ...) {
  if (!file) {
    *__errno_location() = EINVAL;
    return -1;
  }
  va_list ap;
  size_t count = 1;
  va_start(ap, arg0);
  for (const char *a = arg0; a; a = va_arg(ap, const char *))
    count++;
  va_end(ap);
  char **argv = malloc((count + 1) * sizeof(*argv));
  if (!argv)
    return -1;
  va_start(ap, arg0);
  argv[0] = (char *)arg0;
  for (size_t i = 1; i < count; i++)
    argv[i] = va_arg(ap, char *);
  va_end(ap);
  argv[count] = 0;
  int r = path_exec(file, argv);
  int saved = *__errno_location();
  free(argv);
  *__errno_location() = saved;
  return r;
}
