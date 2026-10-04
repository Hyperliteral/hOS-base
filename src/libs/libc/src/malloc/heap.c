// Brk-backed heap shared by malloc/free and sbrk/brk.
//
// This is the allocator from the original single-file libc.c, factored out
// so every file in this directory (and src/unistd/sbrk.c, src/unistd/brk.c)
// shares one view of the heap extent. The design is unchanged on purpose: a
// first-fit list of blocks grown with SYS_brk, no mmap fallback, no threads.
// musl's mallocng is deliberately NOT vendored here - it depends on musl's
// thread/malloc internals and on mmap semantics this kernel does not provide
// for the Mach-O path.
#include <stdint.h>

#include "internal/heap.h"
#include "internal/syscall.h"

static uintptr_t      heap_base;
static uintptr_t      heap_break;
static CavosHeapBlock *heap_first;
static int             heap_ready;

void __cavos_heap_init(void) {
  if (heap_ready)
    return;
  long current = __syscall1(SYS_brk, 0);
  if (current <= 0) {
    *__errno_location() = 12; // ENOMEM
    return;
  }
  heap_base = (uintptr_t)current;
  heap_break = heap_base;
  heap_first = 0;
  heap_ready = 1;
}

size_t __cavos_heap_align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

uintptr_t __cavos_heap_break(void) {
  __cavos_heap_init();
  return heap_break;
}

void *__cavos_heap_grow(size_t increment) {
  __cavos_heap_init();
  if (!heap_ready)
    return 0;
  if (increment > UINTPTR_MAX - heap_break) {
    *__errno_location() = 12; // ENOMEM
    return 0;
  }
  uintptr_t start = heap_break;
  uintptr_t end = start + increment;
  if (__syscall1(SYS_brk, (long)end) != (long)end) {
    *__errno_location() = 12; // ENOMEM
    return 0;
  }
  heap_break = end;
  return (void *)start;
}

CavosHeapBlock *__cavos_heap_find_free(size_t size) {
  for (CavosHeapBlock *block = heap_first; block; block = block->next) {
    if (!block->free || block->size < size)
      continue;
    return block;
  }
  return 0;
}

void __cavos_heap_split(CavosHeapBlock *block, size_t size) {
  if (size > block->size ||
      block->size - size < sizeof(CavosHeapBlock) + CAVOS_HEAP_ALIGNMENT)
    return;
  size_t remainder_size = block->size - size - sizeof(CavosHeapBlock);
  if (remainder_size < CAVOS_HEAP_ALIGNMENT)
    return;
  CavosHeapBlock *remainder =
      (CavosHeapBlock *)((uint8_t *)block + sizeof(CavosHeapBlock) + size);
  remainder->size = remainder_size - sizeof(CavosHeapBlock);
  remainder->next = block->next;
  remainder->magic = CAVOS_HEAP_MAGIC;
  remainder->free = 1;
  block->size = size;
  block->next = remainder;
}

void *__cavos_heap_expand(size_t total) {
  CavosHeapBlock *block = __cavos_heap_grow(total);
  if (!block)
    return 0;
  block->size = total - sizeof(CavosHeapBlock);
  block->next = 0;
  block->magic = CAVOS_HEAP_MAGIC;
  block->free = 0;
  if (!heap_first) {
    heap_first = block;
  } else {
    CavosHeapBlock *last = heap_first;
    while (last->next)
      last = last->next;
    last->next = block;
  }
  return (uint8_t *)block + sizeof(CavosHeapBlock);
}

// Lookup used by free/realloc. Returns 0 for foreign or corrupt pointers;
// freeing those is a silent no-op, matching the old behaviour.
CavosHeapBlock *__cavos_heap_lookup(void *pointer, uintptr_t *base_out) {
  __cavos_heap_init();
  if (!heap_ready)
    return 0;
  uintptr_t address = (uintptr_t)pointer - sizeof(CavosHeapBlock);
  if (address < heap_base || address >= heap_break)
    return 0;
  if (base_out)
    *base_out = heap_base;
  for (CavosHeapBlock *block = heap_first; block; block = block->next) {
    if (block == (CavosHeapBlock *)address)
      return block;
  }
  return 0;
}
