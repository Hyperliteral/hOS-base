// Mutexes, musl-compatible field layout:
// type=__i[0] (low 4 bits), lock=__vi[1], waiters=__vi[2], owner=__p[3],
// count=__i[5]. The lock word holds 0 (free), 1 (held, NORMAL), or the
// owner's tid (RECURSIVE/ERRORCHECK).
// ROBUST and PRIO_* need kernel set_robust_list/PI support that does not
// exist, so init rejects them with ENOTSUP instead of silently degrading.
#include <errno.h>
#include <pthread.h>
#include <limits.h>

#include "internal/pthread_impl.h"

#define MT_NORMAL 0
#define MT_RECURSIVE 1
#define MT_ERRORCHECK 2

static int m_type(const pthread_mutex_t *m) { return m->__u.__i[0] & 15; }

int pthread_mutex_init(pthread_mutex_t *__restrict m,
                       const pthread_mutexattr_t *__restrict attr) {
  if (!m)
    return EINVAL;
  int type = PTHREAD_MUTEX_NORMAL, robust = 0, proto = 0;
  if (attr) {
    type = attr->__attr & 15;
    robust = (attr->__attr >> 4) & 3;
    proto = (attr->__attr >> 8) & 3;
  }
  if (robust != PTHREAD_MUTEX_STALLED || proto != PTHREAD_PRIO_NONE)
    return ENOTSUP;
  if (type != MT_NORMAL && type != MT_RECURSIVE && type != MT_ERRORCHECK)
    return EINVAL;
  m->__u.__i[0] = type;
  m->__u.__vi[1] = 0;
  m->__u.__vi[2] = 0;
  m->__u.__p[3] = 0;
  m->__u.__p[4] = 0;
  m->__u.__i[5] = 0;
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
  if (!m)
    return EINVAL;
  if (m->__u.__vi[1])
    return EBUSY;
  return 0;
}

static int owner_mark(void) {
  struct __pthread *self = __cavos_self();
  int tid = self ? self->tid : 0;
  return tid ? tid : 1;
}

// Acquire, optionally with a relative timeout (NULL = infinite). On success
// the owner cell/count are set for non-NORMAL types.
static int lock_slow(pthread_mutex_t *m, int type, int mark,
                     const struct timespec *relto) {
  struct __pthread *self = __cavos_self();
  for (;;) {
    int cur = __atomic_load_n(&m->__u.__vi[1], __ATOMIC_ACQUIRE);
    if (cur == 0) {
      int expected = 0;
      if (__atomic_compare_exchange_n(&m->__u.__vi[1], &expected, mark, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (type != MT_NORMAL) {
          m->__u.__p[3] = self;
          if (type == MT_RECURSIVE)
            m->__u.__i[5] = 1;
        }
        return 0;
      }
      continue;
    }
    if (type != MT_NORMAL && m->__u.__p[3] == self) {
      if (type == MT_RECURSIVE) {
        m->__u.__i[5]++;
        return 0;
      }
      return EDEADLK;
    }
    __atomic_fetch_add(&m->__u.__vi[2], 1, __ATOMIC_RELAXED);
    int r = __cavos_futex_wait(&m->__u.__vi[1], cur, relto);
    __atomic_fetch_sub(&m->__u.__vi[2], 1, __ATOMIC_RELAXED);
    if (r == 0 || *__errno_location() == EAGAIN)
      continue;
    if (*__errno_location() == EINTR)
      continue;
    return *__errno_location() == ETIMEDOUT ? ETIMEDOUT : *__errno_location();
  }
}

static int do_lock(pthread_mutex_t *m, const struct timespec *relto) {
  int type = m_type(m);
  int mark = type == MT_NORMAL ? 1 : owner_mark();
  int cur = __atomic_load_n(&m->__u.__vi[1], __ATOMIC_ACQUIRE);
  if (cur == 0) {
    int expected = 0;
    if (__atomic_compare_exchange_n(&m->__u.__vi[1], &expected, mark, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      if (type != MT_NORMAL) {
        m->__u.__p[3] = __cavos_self();
        if (type == MT_RECURSIVE)
          m->__u.__i[5] = 1;
      }
      return 0;
    }
  } else if (type != MT_NORMAL && m->__u.__p[3] == __cavos_self()) {
    if (type == MT_RECURSIVE) {
      m->__u.__i[5]++;
      return 0;
    }
    return EDEADLK;
  }
  // Early expiry check for the timed path is done by the caller; an already
  // expired relto makes futex return ETIMEDOUT on the first wait.
  return lock_slow(m, type, mark, relto);
}

int pthread_mutex_lock(pthread_mutex_t *m) {
  if (!m)
    return EINVAL;
  return do_lock(m, 0);
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
  if (!m)
    return EINVAL;
  int               type = m_type(m);
  struct __pthread *self = __cavos_self();
  int cur = __atomic_load_n(&m->__u.__vi[1], __ATOMIC_ACQUIRE);
  if (cur != 0) {
    if (type != MT_NORMAL && m->__u.__p[3] == self) {
      if (type == MT_RECURSIVE) {
        m->__u.__i[5]++;
        return 0;
      }
    }
    return EBUSY;
  }
  int mark = type == MT_NORMAL ? 1 : owner_mark();
  int expected = 0;
  if (!__atomic_compare_exchange_n(&m->__u.__vi[1], &expected, mark, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return EBUSY;
  if (type != MT_NORMAL) {
    m->__u.__p[3] = self;
    if (type == MT_RECURSIVE)
      m->__u.__i[5] = 1;
  }
  return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *__restrict m,
                            const struct timespec *__restrict abstime) {
  if (!m)
    return EINVAL;
  struct timespec rel;
  int r = __cavos_abstime_to_rel(abstime, &rel);
  if (r)
    return r;
  if (rel.tv_sec == 0 && rel.tv_nsec == 0)
    return pthread_mutex_trylock(m);
  return do_lock(m, &rel);
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
  if (!m)
    return EINVAL;
  int type = m_type(m);
  if (type != MT_NORMAL) {
    if (m->__u.__p[3] != __cavos_self())
      return EPERM;
    if (type == MT_RECURSIVE) {
      if (--m->__u.__i[5] > 0)
        return 0;
    }
    m->__u.__p[3] = 0;
  }
  __atomic_store_n(&m->__u.__vi[1], 0, __ATOMIC_RELEASE);
  if (__atomic_load_n(&m->__u.__vi[2], __ATOMIC_ACQUIRE))
    __cavos_futex_wake(&m->__u.__vi[1], 1);
  return 0;
}

int pthread_mutex_consistent(pthread_mutex_t *m) {
  (void)m;
  return ENOTSUP; // no robust mutexes: nothing to mark consistent
}

int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict m,
                                int *__restrict prio) {
  (void)m;
  (void)prio;
  return ENOTSUP;
}

int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict m, int prio,
                                 int *__restrict old) {
  (void)m;
  (void)prio;
  (void)old;
  return ENOTSUP;
}
