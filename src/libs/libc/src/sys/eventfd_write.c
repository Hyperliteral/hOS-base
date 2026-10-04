#include <sys/eventfd.h>
#include <unistd.h>

int eventfd_write(int fd, eventfd_t value) {
  if (write(fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) return -1;
  return 0;
}
