#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void perror(const char *s) {
  int saved = *__errno_location();
  const char *message = strerror(saved);
  size_t length = strlen(message);
  if (s && *s) {
    size_t prefix = strlen(s);
    write(STDERR_FILENO, s, prefix);
    write(STDERR_FILENO, ": ", 2);
  }
  write(STDERR_FILENO, message, length);
  write(STDERR_FILENO, "\n", 1);
  *__errno_location() = saved;
}
