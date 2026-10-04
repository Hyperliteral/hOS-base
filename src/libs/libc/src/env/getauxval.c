// getauxval by reading /proc/self/auxv: pairs of (type, value) unsigned
// longs terminated by AT_NULL. musl reads the aux vector passed at startup;
// this libc's entry path does not retain it, so the kernel-provided file is
// parsed on each call instead. Miss sets ENOENT; querying AT_NULL itself is
// EINVAL (there is no value for the terminator).
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/auxv.h>
#include <unistd.h>

unsigned long getauxval(unsigned long type) {
  if (type == AT_NULL) {
    *__errno_location() = EINVAL;
    return 0;
  }
  int fd = open("/proc/self/auxv", O_RDONLY);
  if (fd < 0) {
    *__errno_location() = ENOENT;
    return 0;
  }
  unsigned long pair[2];
  for (;;) {
    size_t got = 0;
    while (got < sizeof(pair)) {
      ssize_t r =
          read(fd, (char *)pair + got, sizeof(pair) - got);
      if (r < 0) {
        int saved = *__errno_location();
        close(fd);
        *__errno_location() = saved;
        return 0;
      }
      if (r == 0)
        break;
      got += (size_t)r;
    }
    if (got < sizeof(pair)) {
      close(fd);
      *__errno_location() = ENOENT;
      return 0;
    }
    if (pair[0] == AT_NULL) {
      close(fd);
      *__errno_location() = ENOENT;
      return 0;
    }
    if (pair[0] == type) {
      close(fd);
      return pair[1];
    }
  }
}
