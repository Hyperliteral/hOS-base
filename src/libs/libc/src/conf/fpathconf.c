#include <unistd.h>

long fpathconf(int fd, int name) {
  (void)fd;
  // Same constants as pathconf: nothing here is fd-dependent.
  return pathconf(0, name);
}
