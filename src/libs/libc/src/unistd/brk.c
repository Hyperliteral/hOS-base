#include <stdint.h>
#include <unistd.h>

#include "internal/heap.h"
#include "internal/syscall.h"

// musl signature: int brk(void *). Moving the break backwards is rejected -
// the heap list owns everything below the current break and cannot release it.
int brk(void *address) {
  uintptr_t target = (uintptr_t)address;
  uintptr_t current = __cavos_heap_break();
  if (target < current)
    return -1;
  if (target == current)
    return 0;
  if (!__cavos_heap_grow((size_t)(target - current)))
    return -1;
  return 0;
}
