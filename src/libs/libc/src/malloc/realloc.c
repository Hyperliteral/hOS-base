#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal/heap.h"
#include "internal/syscall.h"

void *realloc(void *pointer, size_t size) {
  if (!pointer)
    return malloc(size);
  if (size == 0) {
    free(pointer);
    return 0;
  }
  if (size > SIZE_MAX - sizeof(CavosHeapBlock) - (CAVOS_HEAP_ALIGNMENT - 1)) {
    *__errno_location() = ENOMEM;
    return 0;
  }
  size_t allocation_size = __cavos_heap_align_up(size, CAVOS_HEAP_ALIGNMENT);
  if (allocation_size < size) {
    *__errno_location() = ENOMEM;
    return 0;
  }
  __cavos_heap_lock();
  CavosHeapBlock *block = __cavos_heap_lookup(pointer, 0);
  if (!block || block->magic != CAVOS_HEAP_MAGIC || block->free) {
    __cavos_heap_unlock();
    return 0;
  }
  if (block->size >= allocation_size) {
    block->free = 0;
    __cavos_heap_split(block, allocation_size);
    __cavos_heap_unlock();
    return pointer;
  }
  __cavos_heap_unlock();
  void *replacement = malloc(size);
  if (!replacement)
    return 0;
  memcpy(replacement, pointer, block->size);
  free(pointer);
  return replacement;
}
