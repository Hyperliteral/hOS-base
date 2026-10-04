#include <stdio.h>
#include <unistd.h>

// No kernel pwrite64: best-effort lseek + write. This races with concurrent
// seeks, writes, or pwrites on the same fd; there is no kernel op to do better.
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
  if (lseek(fd, offset, SEEK_SET) == (off_t)-1) return -1;
  return write(fd, buf, count);
}
