#include <stdint.h>
#include <unistd.h>

#include "internal/heap.h"
#include "internal/syscall.h"

// sbrk shares the heap extent with malloc (see src/malloc/heap.c): growing
// here moves the same break the allocator expands, so the two cannot drift
// apart. Only forward growth is supported; the kernel has no shrink path.
void *sbrk(intptr_t increment) {
  if (increment < 0) {
    *__errno_location() = 12; // ENOMEM
    return (void *)-1;
  }
  if (increment == 0)
    return (void *)__cavos_heap_break();
  if (!__cavos_heap_grow((size_t)increment))
    return (void *)-1;
  return (void *)(__cavos_heap_break() - (uintptr_t)increment);
}
