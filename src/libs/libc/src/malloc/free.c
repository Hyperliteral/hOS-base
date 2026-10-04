#include <stdint.h>
#include <stdlib.h>

#include "internal/heap.h"

void free(void *pointer) {
  if (!pointer)
    return;
  CavosHeapBlock *block = __cavos_heap_lookup(pointer, 0);
  if (!block || block->magic != CAVOS_HEAP_MAGIC || block->free)
    return;
  block->free = 1;
  if (block->next && block->next->magic == CAVOS_HEAP_MAGIC &&
      block->next->free) {
    CavosHeapBlock *next = block->next;
    block->size += sizeof(CavosHeapBlock) + next->size;
    block->next = next->next;
  }
}
