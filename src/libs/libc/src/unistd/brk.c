#include <stdint.h>
#include <unistd.h>

#include "internal/heap.h"
#include "internal/syscall.h"

// musl signature: int brk(void *). Moving the break backwards is rejected -
// the heap list owns everything below the current break and cannot release it.
int brk(void *address) {
  uintptr_t target = (uintptr_t)address;
  __cavos_heap_lock();
  uintptr_t current = __cavos_heap_break();
  if (target < current) {
    __cavos_heap_unlock();
    return -1;
  }
  if (target == current) {
    __cavos_heap_unlock();
    return 0;
  }
  int ok = __cavos_heap_grow((size_t)(target - current)) != 0;
  __cavos_heap_unlock();
  return ok ? 0 : -1;
}
