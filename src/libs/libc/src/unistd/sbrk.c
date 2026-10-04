#include <stdint.h>
#include <unistd.h>

#include "internal/heap.h"
#include "internal/syscall.h"

// sbrk shares the heap extent with malloc (see src/malloc/heap.c): growing
// here moves the same break the allocator expands, so the two cannot drift
// apart. Only forward growth is supported; the kernel has no shrink path.
void *sbrk(intptr_t increment) {
  if (increment < 0) {
    *__errno_location() = ENOMEM;
    return (void *)-1;
  }
  __cavos_heap_lock();
  if (increment == 0) {
    void *out = (void *)__cavos_heap_break();
    __cavos_heap_unlock();
    return out;
  }
  if (!__cavos_heap_grow((size_t)increment)) {
    __cavos_heap_unlock();
    return (void *)-1;
  }
  void *out = (void *)(__cavos_heap_break() - (uintptr_t)increment);
  __cavos_heap_unlock();
  return out;
}
