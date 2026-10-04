#include <sys/stat.h>

int mkfifo(const char *path, mode_t mode) {
  return mknod(path, mode | S_IFIFO, (dev_t)0);
}
