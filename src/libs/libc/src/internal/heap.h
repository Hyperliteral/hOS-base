// Shared state for the brk-backed heap (see src/malloc/heap.c).
//
// The allocator grows a single contiguous extent via SYS_brk, so malloc, free
// and sbrk/brk must agree on where the extent ends. That state lives in
// heap.c and is shared through these helpers; every symbol is prefixed to
// stay out of the way of the public musl-compatible API.
#ifndef CAVOS_INTERNAL_HEAP_H
#define CAVOS_INTERNAL_HEAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct CavosHeapBlock {
  size_t                  size;
  struct CavosHeapBlock  *next;
  uint32_t                magic;
  uint32_t                free;
  uint8_t                 padding[8];
} CavosHeapBlock;

// Initialise the heap from the current break. Safe to call repeatedly.
void __cavos_heap_init(void);

// Break management shared with sbrk()/brk().
uintptr_t __cavos_heap_break(void);
void     *__cavos_heap_grow(size_t increment);

// Block-list primitives used by malloc/free/realloc.
CavosHeapBlock *__cavos_heap_find_free(size_t size);
CavosHeapBlock *__cavos_heap_lookup(void *pointer, uintptr_t *base_out);
void            __cavos_heap_split(CavosHeapBlock *block, size_t size);
void           *__cavos_heap_expand(size_t total);
size_t          __cavos_heap_align_up(size_t value, size_t alignment);

// Freelist lock: short spin around list ops (malloc/free/realloc). Leaf-level
// only - realloc delegates to the locking malloc/free instead of holding it,
// so no path nests. sbrk/brk take it too, since they move the same extent.
void __cavos_heap_lock(void);
void __cavos_heap_unlock(void);

#define CAVOS_HEAP_ALIGNMENT 16
#define CAVOS_HEAP_MAGIC     0x4d41434fU

#endif
