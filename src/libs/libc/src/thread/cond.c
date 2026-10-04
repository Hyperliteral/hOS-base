// Condition variables. Same union as musl's (lock=__vi[8], seq=__vi[2],
// waiters=__vi[3]); the waiter-queue pointers musl keeps in __p[1]/__p[5]
// are unused - wakeups go through the sequence counter, which is all a
// zero-initialised cond needs. CLOCK_REALTIME (default) and CLOCK_MONOTONIC
// clocks are accepted; the kernel takes relative timeouts (see futex.c).
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "internal/pthread_impl.h"

static volatile int *c_lock(pthread_cond_t *c) { return &c->__u.__vi[8]; }
static volatile int *c_seq(pthread_cond_t *c) { return &c->__u.__vi[2]; }
static volatile int *c_waiters(pthread_cond_t *c) { return &c->__u.__vi[3]; }

static void spin_lock(volatile int *w) {
  while (__atomic_exchange_n(w, 1, __ATOMIC_ACQUIRE))
    __asm__ __volatile__("pause");
}

static void spin_unlock(volatile int *w) {
  __atomic_store_n(w, 0, __ATOMIC_RELEASE);
}

int pthread_cond_init(pthread_cond_t *__restrict c,
                      const pthread_condattr_t *__restrict attr) {
  if (!c)
    return EINVAL;
  if (attr) {
    int clock = attr->__attr & 0xffff;
    int shared = (attr->__attr >> 16) & 0xffff;
    if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
      return EINVAL;
    if (shared != PTHREAD_PROCESS_PRIVATE && shared != PTHREAD_PROCESS_SHARED)
      return EINVAL;
    c->__u.__i[4] = clock;
  } else {
    c->__u.__i[4] = CLOCK_REALTIME;
  }
  c->__u.__vi[8] = 0;
  c->__u.__vi[2] = 0;
  c->__u.__vi[3] = 0;
  c->__u.__p[0] = 0;
  c->__u.__p[1] = 0;
  c->__u.__p[5] = 0;
  return 0;
}

int pthread_cond_destroy(pthread_cond_t *c) {
  if (!c)
    return EINVAL;
  if (__atomic_load_n(c_waiters(c), __ATOMIC_ACQUIRE))
    return EBUSY;
  return 0;
}

static int wait_inner(pthread_cond_t *c, pthread_mutex_t *m,
                      const struct timespec *relto) {
  int seq = __atomic_load_n(c_seq(c), __ATOMIC_ACQUIRE);
  spin_lock(c_lock(c));
  __atomic_fetch_add(c_waiters(c), 1, __ATOMIC_RELAXED);
  spin_unlock(c_lock(c));
  pthread_mutex_unlock(m);
  int r = __cavos_futex_wait(c_seq(c), seq, relto);
  int e = r == 0 ? 0 : *__errno_location();
  spin_lock(c_lock(c));
  __atomic_fetch_sub(c_waiters(c), 1, __ATOMIC_RELAXED);
  spin_unlock(c_lock(c));
  pthread_mutex_lock(m);
  // EAGAIN: the sequence moved before we slept (signal won the race) -
  // a normal wakeup. EINTR: a pending signal interrupted the sleep; cond
  // wait is specified to return with the predicate re-checked by the
  // caller, so report success and let the loop decide.
  if (r == 0 || e == EAGAIN || e == EINTR)
    return 0;
  return e == ETIMEDOUT ? ETIMEDOUT : e;
}

int pthread_cond_wait(pthread_cond_t *__restrict c,
                      pthread_mutex_t *__restrict m) {
  if (!c || !m)
    return EINVAL;
  // Cancellation point (deferred): act on a pending request now.
  __cavos_testcancel();
  int r = wait_inner(c, m, 0);
  __cavos_testcancel();
  return r;
}

int pthread_cond_timedwait(pthread_cond_t *__restrict c,
                           pthread_mutex_t *__restrict m,
                           const struct timespec *__restrict abstime) {
  if (!c || !m)
    return EINVAL;
  if (!abstime || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
    return EINVAL;
  struct timespec rel;
  // Only CLOCK_REALTIME abstimes are converted here; a MONOTONIC cond
  // converts against the monotonic clock instead.
  if (c->__u.__i[4] == CLOCK_MONOTONIC) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
      return EINVAL;
    long sec = abstime->tv_sec - now.tv_sec;
    long nsec = abstime->tv_nsec - now.tv_nsec;
    if (nsec < 0) {
      sec--;
      nsec += 1000000000L;
    }
    if (sec < 0) {
      rel.tv_sec = 0;
      rel.tv_nsec = 0;
    } else {
      rel.tv_sec = sec;
      rel.tv_nsec = nsec;
    }
  } else {
    int e = __cavos_abstime_to_rel(abstime, &rel);
    if (e)
      return e;
  }
  if (rel.tv_sec == 0 && rel.tv_nsec == 0)
    return ETIMEDOUT;
  __cavos_testcancel();
  int r = wait_inner(c, m, &rel);
  __cavos_testcancel();
  return r;
}

int pthread_cond_signal(pthread_cond_t *c) {
  if (!c)
    return EINVAL;
  spin_lock(c_lock(c));
  int waiters = __atomic_load_n(c_waiters(c), __ATOMIC_RELAXED);
  if (waiters > 0) {
    __atomic_fetch_add(c_seq(c), 1, __ATOMIC_RELEASE);
    spin_unlock(c_lock(c));
    __cavos_futex_wake(c_seq(c), 1);
    return 0;
  }
  spin_unlock(c_lock(c));
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c) {
  if (!c)
    return EINVAL;
  spin_lock(c_lock(c));
  int waiters = __atomic_load_n(c_waiters(c), __ATOMIC_RELAXED);
  if (waiters > 0) {
    __atomic_fetch_add(c_seq(c), 1, __ATOMIC_RELEASE);
    spin_unlock(c_lock(c));
    __cavos_futex_wake(c_seq(c), waiters);
    return 0;
  }
  spin_unlock(c_lock(c));
  return 0;
}
