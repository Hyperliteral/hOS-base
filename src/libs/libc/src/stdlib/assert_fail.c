// __assert_fail backs assert(): report to fd 2 with a raw write (dprintf is
// not implemented in this libc) and abort. Format matches musl's.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void __assert_fail(const char *expr, const char *file, int line,
                   const char *func) {
  char buf[512];
  int len = snprintf(buf, sizeof(buf),
                     "Assertion `%s' failed at %s:%d in %s\n",
                     expr ? expr : "?", file ? file : "?",
                     line, func ? func : "?");
  if (len > 0) {
    if ((size_t)len > sizeof(buf))
      len = (int)sizeof(buf);
    write(STDERR_FILENO, buf, (size_t)len);
  }
  abort();
}
