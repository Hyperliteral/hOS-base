// Spinlocks, barriers, once. Spins pause + yield so a preempted holder does
// not livelock the spinner on a single CPU.
#include <errno.h>
#include <pthread.h>
#include <sched.h>

#include "internal/pthread_impl.h"

int pthread_spin_init(pthread_spinlock_t *s, int shared) {
  if (!s)
    return EINVAL;
  (void)shared;
  *s = 0;
  return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *s) {
  if (!s)
    return EINVAL;
  return 0;
}

int pthread_spin_lock(pthread_spinlock_t *s) {
  if (!s)
    return EINVAL;
  while (__atomic_exchange_n(s, 1, __ATOMIC_ACQUIRE))
    sched_yield();
  return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *s) {
  if (!s)
    return EINVAL;
  return __atomic_exchange_n(s, 1, __ATOMIC_ACQUIRE) ? EBUSY : 0;
}

int pthread_spin_unlock(pthread_spinlock_t *s) {
  if (!s)
    return EINVAL;
  __atomic_store_n(s, 0, __ATOMIC_RELEASE);
  return 0;
}

// Barrier: count=__i[0], arrived=__i[1], generation(seq)=__i[2].
int pthread_barrier_init(pthread_barrier_t *__restrict b,
                         const pthread_barrierattr_t *__restrict attr,
                         unsigned count) {
  if (!b || count == 0)
    return EINVAL;
  (void)attr;
  b->__u.__i[0] = (int)count;
  b->__u.__i[1] = 0;
  b->__u.__i[2] = 0;
  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
  if (!b)
    return EINVAL;
  if (b->__u.__i[1] != 0)
    return EBUSY;
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b) {
  if (!b)
    return EINVAL;
  int gen = __atomic_load_n(&b->__u.__i[2], __ATOMIC_ACQUIRE);
  int arrived = __atomic_fetch_add(&b->__u.__i[1], 1, __ATOMIC_ACQ_REL) + 1;
  if (arrived == b->__u.__i[0]) {
    __atomic_store_n(&b->__u.__i[1], 0, __ATOMIC_RELAXED);
    __atomic_fetch_add(&b->__u.__i[2], 1, __ATOMIC_RELEASE);
    __cavos_futex_wake(&b->__u.__i[2], 0x7fffffff);
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
  while (__atomic_load_n(&b->__u.__i[2], __ATOMIC_ACQUIRE) == gen)
    __cavos_futex_wait(&b->__u.__i[2], gen, 0);
  return 0;
}

int pthread_once(pthread_once_t *once, void (*fn)(void)) {
  if (!once || !fn)
    return EINVAL;
  int st = __atomic_load_n(once, __ATOMIC_ACQUIRE);
  if (st == 2)
    return 0;
  if (st == 0) {
    int expected = 0;
    if (__atomic_compare_exchange_n(once, &expected, 1, 0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
      fn();
      __atomic_store_n(once, 2, __ATOMIC_RELEASE);
      __cavos_futex_wake(once, 0x7fffffff);
      return 0;
    }
  }
  while (__atomic_load_n(once, __ATOMIC_ACQUIRE) == 1)
    __cavos_futex_wait(once, 1, 0);
  return 0;
}
