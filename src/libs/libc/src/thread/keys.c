// Thread-specific data: 128 process-wide keys with destructors.
// Values live in the TCB; destructors run in __cavos_thread_entry /
// pthread_exit, up to PTHREAD_DESTRUCTOR_ITERATIONS passes.
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "internal/pthread_impl.h"

static void (*key_dtors[PTHREAD_KEYS_MAX])(void *);
static unsigned key_gen[PTHREAD_KEYS_MAX];
static unsigned key_next_gen = 1;
static volatile int key_lock;

static void keys_lock(void) {
  while (__atomic_exchange_n(&key_lock, 1, __ATOMIC_ACQUIRE))
    __asm__ __volatile__("pause");
}

static void keys_unlock(void) {
  __atomic_store_n(&key_lock, 0, __ATOMIC_RELEASE);
}

int pthread_key_create(pthread_key_t *key, void (*dtor)(void *)) {
  if (!key)
    return EINVAL;
  keys_lock();
  for (pthread_key_t i = 0; i < PTHREAD_KEYS_MAX; i++) {
    // A live key has a dtor slot claimed (any generation); tombstones and
    // never-used slots are both free. Generations (below) keep recycled
    // slots from exposing other threads' stale values.
    if (key_dtors[i] && key_dtors[i] != (void (*)(void *))-1)
      continue;
    key_dtors[i] = dtor;
    if (++key_next_gen == 0)
      ++key_next_gen;
    key_gen[i] = key_next_gen;
    *key = i;
    keys_unlock();
    return 0;
  }
  keys_unlock();
  return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
  if (key >= PTHREAD_KEYS_MAX)
    return EINVAL;
  keys_lock();
  if (!key_dtors[key] || key_dtors[key] == (void (*)(void *))-1) {
    keys_unlock();
    return EINVAL;
  }
  key_dtors[key] = (void (*)(void *))-1; // tombstone: slot recyclable
  if (++key_next_gen == 0)
    ++key_next_gen;
  key_gen[key] = key_next_gen; // stale values in live threads go invisible
  keys_unlock();
  return 0;
}

void *pthread_getspecific(pthread_key_t key) {
  if (key >= PTHREAD_KEYS_MAX)
    return 0;
  struct __pthread *t = __cavos_self();
  if (!t)
    return 0;
  if (!key_dtors[key] || key_dtors[key] == (void (*)(void *))-1)
    return 0;
  if (t->tsd_gen[key] != key_gen[key])
    return 0;
  return t->tsd[key];
}

int pthread_setspecific(pthread_key_t key, const void *value) {
  if (key >= PTHREAD_KEYS_MAX)
    return EINVAL;
  struct __pthread *t = __cavos_self();
  if (!t)
    return EINVAL;
  keys_lock();
  int live = key_dtors[key] && key_dtors[key] != (void (*)(void *))-1;
  unsigned gen = key_gen[key];
  keys_unlock();
  if (!live)
    return EINVAL;
  t->tsd[key] = (void *)value;
  t->tsd_gen[key] = gen;
  if (value)
    t->tsd_used = 1;
  return 0;
}

// One destructor pass over live keys; the caller loops it.
void __cavos_tsd_destroy_pass(struct __pthread *t) {
  for (pthread_key_t i = 0; i < PTHREAD_KEYS_MAX; i++) {
    void (*dtor)(void *) = key_dtors[i];
    if (!dtor || dtor == (void (*)(void *))-1)
      continue;
    if (t->tsd_gen[i] != key_gen[i])
      continue;
    void *v = t->tsd[i];
    if (!v)
      continue;
    t->tsd[i] = 0;
    dtor(v);
  }
}
