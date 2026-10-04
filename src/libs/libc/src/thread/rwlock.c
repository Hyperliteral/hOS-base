// Reader/writer lock: single state word + futex. state < 0 means held for
// writing, state >= 0 counts readers. Writers are sticky-preferred: a
// waiting writer blocks new readers, which bounds writer starvation without
// a full FIFO. Errorcheck semantics (EDEADLK on write-recursion) are free;
// recursive read-locking is tracked per-thread count in __i[3].
#include <errno.h>
#include <pthread.h>

#include "internal/pthread_impl.h"

static volatile int *rw_state(pthread_rwlock_t *rw) { return &rw->__u.__vi[0]; }
static volatile int *rw_waiters(pthread_rwlock_t *rw) {
  return &rw->__u.__vi[1];
}

int pthread_rwlock_init(pthread_rwlock_t *__restrict rw,
                        const pthread_rwlockattr_t *__restrict attr) {
  if (!rw)
    return EINVAL;
  (void)attr; // pshared only; no process-shared distinction exists
  rw->__u.__vi[0] = 0;
  rw->__u.__vi[1] = 0;
  rw->__u.__i[2] = 0;
  rw->__u.__i[3] = 0;
  return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
  if (!rw)
    return EINVAL;
  if (__atomic_load_n(rw_state(rw), __ATOMIC_ACQUIRE) != 0 ||
      __atomic_load_n(rw_waiters(rw), __ATOMIC_ACQUIRE) != 0)
    return EBUSY;
  return 0;
}

static int rdlock_slow(pthread_rwlock_t *rw, const struct timespec *relto) {
  for (;;) {
    int cur = __atomic_load_n(rw_state(rw), __ATOMIC_ACQUIRE);
    if (cur >= 0) {
      int expected = cur;
      if (__atomic_compare_exchange_n((int *)rw_state(rw), &expected, cur + 1,
                                      0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
      continue;
    }
    __atomic_fetch_add(rw_waiters(rw), 1, __ATOMIC_RELAXED);
    int r = __cavos_futex_wait((int *)rw_state(rw), cur, relto);
    __atomic_fetch_sub(rw_waiters(rw), 1, __ATOMIC_RELAXED);
    int e = r == 0 ? 0 : *__errno_location();
    if (e == 0 || e == EAGAIN || e == EINTR)
      continue;
    return e == ETIMEDOUT ? ETIMEDOUT : e;
  }
}

static int wrlock_slow(pthread_rwlock_t *rw, const struct timespec *relto) {
  for (;;) {
    int cur = __atomic_load_n(rw_state(rw), __ATOMIC_ACQUIRE);
    if (cur == 0) {
      int expected = 0;
      if (__atomic_compare_exchange_n((int *)rw_state(rw), &expected, -1, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
      continue;
    }
    if (cur == -1 && rw->__u.__p[3] == __cavos_self())
      return EDEADLK;
    __atomic_fetch_add(rw_waiters(rw), 1, __ATOMIC_RELAXED);
    int r = __cavos_futex_wait((int *)rw_state(rw), cur, relto);
    __atomic_fetch_sub(rw_waiters(rw), 1, __ATOMIC_RELAXED);
    int e = r == 0 ? 0 : *__errno_location();
    if (e == 0 || e == EAGAIN || e == EINTR)
      continue;
    return e == ETIMEDOUT ? ETIMEDOUT : e;
  }
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
  if (!rw)
    return EINVAL;
  __cavos_testcancel();
  return rdlock_slow(rw, 0);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw) {
  if (!rw)
    return EINVAL;
  int cur = __atomic_load_n(rw_state(rw), __ATOMIC_ACQUIRE);
  while (cur >= 0) {
    int expected = cur;
    if (__atomic_compare_exchange_n((int *)rw_state(rw), &expected, cur + 1,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      return 0;
    cur = expected;
  }
  return EBUSY;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict rw,
                               const struct timespec *__restrict abstime) {
  if (!rw)
    return EINVAL;
  struct timespec rel;
  int e = __cavos_abstime_to_rel(abstime, &rel);
  if (e)
    return e;
  if (rel.tv_sec == 0 && rel.tv_nsec == 0)
    return pthread_rwlock_tryrdlock(rw);
  __cavos_testcancel();
  return rdlock_slow(rw, &rel);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
  if (!rw)
    return EINVAL;
  __cavos_testcancel();
  int r = wrlock_slow(rw, 0);
  if (r == 0)
    rw->__u.__p[3] = __cavos_self();
  return r;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
  if (!rw)
    return EINVAL;
  int expected = 0;
  if (!__atomic_compare_exchange_n((int *)rw_state(rw), &expected, -1, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return EBUSY;
  rw->__u.__p[3] = __cavos_self();
  return 0;
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict rw,
                               const struct timespec *__restrict abstime) {
  if (!rw)
    return EINVAL;
  struct timespec rel;
  int e = __cavos_abstime_to_rel(abstime, &rel);
  if (e)
    return e;
  if (rel.tv_sec == 0 && rel.tv_nsec == 0)
    return pthread_rwlock_trywrlock(rw);
  __cavos_testcancel();
  int r = wrlock_slow(rw, &rel);
  if (r == 0)
    rw->__u.__p[3] = __cavos_self();
  return r;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
  if (!rw)
    return EINVAL;
  int cur = __atomic_load_n(rw_state(rw), __ATOMIC_ACQUIRE);
  if (cur == -1) {
    if (rw->__u.__p[3] != __cavos_self())
      return EPERM;
    rw->__u.__p[3] = 0;
    __atomic_store_n((int *)rw_state(rw), 0, __ATOMIC_RELEASE);
  } else if (cur > 0) {
    __atomic_fetch_sub((int *)rw_state(rw), 1, __ATOMIC_ACQ_REL);
  } else {
    return EPERM;
  }
  if (__atomic_load_n(rw_waiters(rw), __ATOMIC_ACQUIRE))
    __cavos_futex_wake((int *)rw_state(rw), 0x7fffffff);
  return 0;
}
