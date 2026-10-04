#include <stdint.h>
#include <stdlib.h>

#include "internal/heap.h"
#include "internal/syscall.h"

void *calloc(size_t count, size_t size) {
  if (size != 0 && count > SIZE_MAX / size) {
    *__errno_location() = ENOMEM;
    return 0;
  }
  size_t total = count * size;
  void *pointer = malloc(total);
  if (!pointer)
    return 0;
  uint8_t *bytes = (uint8_t *)pointer;
  for (size_t i = 0; i < total; i++)
    bytes[i] = 0;
  return pointer;
}
