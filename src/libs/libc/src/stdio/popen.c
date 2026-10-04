// popen/pclose over pipe + fork + dup2 + execl("/bin/sh -c"). The child
// pid rides in the FILE's pipe_pid slot (part of the musl-compatible layout,
// see src/internal/stdio_impl.h). pclose returns the raw waitpid status, so
// callers decode it with WEXITSTATUS/WIFSIGNALED as POSIX requires; a child
// whose exec fails exits 127, matching the shell convention.
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "internal/stdio_impl.h"

FILE *popen(const char *cmd, const char *mode) {
  if (!cmd || !mode || (mode[0] != 'r' && mode[0] != 'w')) {
    *__errno_location() = EINVAL;
    return 0;
  }
  int reading = mode[0] == 'r';
  int p[2];
  if (pipe(p) < 0)
    return 0;
  pid_t pid = fork();
  if (pid < 0) {
    int saved = *__errno_location();
    close(p[0]);
    close(p[1]);
    *__errno_location() = saved;
    return 0;
  }
  if (pid == 0) {
    if (reading) {
      dup2(p[1], STDOUT_FILENO);
    } else {
      dup2(p[0], STDIN_FILENO);
    }
    close(p[0]);
    close(p[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char *)0);
    _exit(127);
  }
  FILE *file;
  if (reading) {
    close(p[1]);
    file = fdopen(p[0], "r");
    if (!file)
      close(p[0]);
  } else {
    close(p[0]);
    file = fdopen(p[1], "w");
    if (!file)
      close(p[1]);
  }
  if (!file) {
    int saved = *__errno_location();
    kill(pid, 9);
    (void)waitpid(pid, 0, 0);
    *__errno_location() = saved;
    return 0;
  }
  file->pipe_pid = pid;
  return file;
}

int pclose(FILE *file) {
  if (!file || file->pipe_pid <= 0) {
    *__errno_location() = EINVAL;
    return -1;
  }
  pid_t pid = file->pipe_pid;
  __cavos_pushback_drop(file);
  if (file->fd >= 0)
    close(file->fd);
  file->fd = -1;
  free(file);
  int status = 0;
  pid_t r;
  do {
    r = waitpid(pid, &status, 0);
  } while (r < 0 && *__errno_location() == EINTR);
  if (r < 0)
    return -1;
  return status;
}
