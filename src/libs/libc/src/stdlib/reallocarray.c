#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
  if (nmemb > 0 && size > SIZE_MAX / nmemb) {
    *__errno_location() = ENOMEM;
    return 0;
  }
  return realloc(ptr, nmemb * size);
}
