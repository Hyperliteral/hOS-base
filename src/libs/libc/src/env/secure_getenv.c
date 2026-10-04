#include <stdlib.h>
#include <unistd.h>

char *secure_getenv(const char *name) {
  if (issetugid())
    return 0;
  return getenv(name);
}
