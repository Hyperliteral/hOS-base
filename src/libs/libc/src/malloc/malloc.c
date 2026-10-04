#include <stdint.h>
#include <stdlib.h>

#include "internal/heap.h"
#include "internal/syscall.h"

void *malloc(size_t size) {
  if (size == 0)
    size = 1;
  if (size > SIZE_MAX - sizeof(CavosHeapBlock) - (CAVOS_HEAP_ALIGNMENT - 1)) {
    *__errno_location() = ENOMEM;
    return 0;
  }
  size_t allocation_size = __cavos_heap_align_up(size, CAVOS_HEAP_ALIGNMENT);
  size_t total = sizeof(CavosHeapBlock) + allocation_size;
  if (allocation_size < size || total < allocation_size) {
    *__errno_location() = ENOMEM;
    return 0;
  }
  __cavos_heap_init();
  __cavos_heap_lock();
  CavosHeapBlock *block = __cavos_heap_find_free(allocation_size);
  if (block) {
    block->free = 0;
    __cavos_heap_split(block, allocation_size);
    __cavos_heap_unlock();
    return (uint8_t *)block + sizeof(CavosHeapBlock);
  }
  void *out = __cavos_heap_expand(total);
  __cavos_heap_unlock();
  return out;
}
