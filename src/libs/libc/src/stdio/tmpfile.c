// tmpfile: anonymous file via mkstemp + immediate unlink. O_TMPFILE is
// unavailable on this kernel path, so the classic create-and-unlink dance
// stands in; the fd stays valid after unlink and the name never collides.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

FILE *tmpfile(void) {
  char tmpl[] = "/tmp/tmpf.XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd < 0)
    return 0;
  unlink(tmpl);
  FILE *file = fdopen(fd, "w+");
  if (!file)
    close(fd);
  return file;
}
