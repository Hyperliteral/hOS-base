// C11 threads (thin wrappers over pthreads) and GNU _np extras.
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "internal/pthread_impl.h"

struct thrd_start {
  thrd_start_t fn;
  void        *arg;
};

static void *thrd_tramp(void *p) {
  struct thrd_start *s = p;
  thrd_start_t       fn = s->fn;
  void              *arg = s->arg;
  free(s);
  int r = fn(arg);
  return (void *)(uintptr_t)r;
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
  if (!thr || !func)
    return thrd_error;
  struct thrd_start *s = malloc(sizeof(*s));
  if (!s)
    return thrd_nomem;
  s->fn = func;
  s->arg = arg;
  pthread_t th;
  int       r = pthread_create(&th, 0, thrd_tramp, s);
  if (r != 0) {
    free(s);
    return r == EAGAIN ? thrd_nomem : thrd_error;
  }
  *thr = (thrd_t)(uintptr_t)(pthread_t)th;
  return thrd_success;
}

// thrd_equal is macro-ified by the header for C callers (like
// pthread_equal); undef for the real definition.
#undef thrd_equal
int thrd_equal(thrd_t a, thrd_t b) { return a == b; }
thrd_t thrd_current(void) {
  return (thrd_t)(uintptr_t)(pthread_t)pthread_self();
}

int thrd_sleep(const struct timespec *dur, struct timespec *rem) {
  if (!dur || dur->tv_sec < 0 || dur->tv_nsec < 0 ||
      dur->tv_nsec >= 1000000000L)
    return -1;
  extern int nanosleep(const struct timespec *, struct timespec *);
  return nanosleep(dur, rem) == 0 ? 0 : -1;
}

void thrd_yield(void) {
  extern int sched_yield(void);
  sched_yield();
}

_Noreturn void thrd_exit(int res) { pthread_exit((void *)(uintptr_t)res); }

int thrd_detach(thrd_t thr) {
  return pthread_detach((pthread_t)(uintptr_t)thr) == 0 ? thrd_success
                                                        : thrd_error;
}

int thrd_join(thrd_t thr, int *res) {
  void *r = 0;
  int   e = pthread_join((pthread_t)(uintptr_t)thr, &r);
  if (e != 0)
    return thrd_error;
  if (res)
    *res = (int)(uintptr_t)r;
  return thrd_success;
}

void call_once(once_flag *flag, void (*fn)(void)) {
  pthread_once(flag, fn);
}

int mtx_init(mtx_t *m, int type) {
  // musl values are bit-combinable (plain=0, recursive=1, timed=2); the
  // timed bit needs no support here since every mutex already times.
  if (!m || type < 0 || type > (mtx_timed | mtx_recursive))
    return thrd_error;
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  if (type & mtx_recursive)
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  int r = pthread_mutex_init((pthread_mutex_t *)m, &a);
  return r == 0 ? thrd_success : thrd_error;
}

int mtx_lock(mtx_t *m) {
  int r = pthread_mutex_lock((pthread_mutex_t *)m);
  return r == 0 ? thrd_success : thrd_error;
}

int mtx_timedlock(mtx_t *__restrict m, const struct timespec *__restrict ts) {
  int r = pthread_mutex_timedlock((pthread_mutex_t *)m, ts);
  if (r == 0)
    return thrd_success;
  return r == ETIMEDOUT ? thrd_timedout : thrd_error;
}

int mtx_trylock(mtx_t *m) {
  int r = pthread_mutex_trylock((pthread_mutex_t *)m);
  if (r == 0)
    return thrd_success;
  return r == EBUSY ? thrd_busy : thrd_error;
}

int mtx_unlock(mtx_t *m) {
  return pthread_mutex_unlock((pthread_mutex_t *)m) == 0 ? thrd_success
                                                         : thrd_error;
}

void mtx_destroy(mtx_t *m) {
  pthread_mutex_destroy((pthread_mutex_t *)m);
}

int cnd_init(cnd_t *c) {
  return pthread_cond_init((pthread_cond_t *)c, 0) == 0 ? thrd_success
                                                        : thrd_error;
}

int cnd_signal(cnd_t *c) {
  return pthread_cond_signal((pthread_cond_t *)c) == 0 ? thrd_success
                                                       : thrd_error;
}

int cnd_broadcast(cnd_t *c) {
  return pthread_cond_broadcast((pthread_cond_t *)c) == 0 ? thrd_success
                                                           : thrd_error;
}

int cnd_wait(cnd_t *c, mtx_t *m) {
  return pthread_cond_wait((pthread_cond_t *)c, (pthread_mutex_t *)m) == 0
             ? thrd_success
             : thrd_error;
}

int cnd_timedwait(cnd_t *__restrict c, mtx_t *__restrict m,
                  const struct timespec *__restrict ts) {
  int r = pthread_cond_timedwait((pthread_cond_t *)c, (pthread_mutex_t *)m,
                                 ts);
  if (r == 0)
    return thrd_success;
  return r == ETIMEDOUT ? thrd_timedout : thrd_error;
}

void cnd_destroy(cnd_t *c) { pthread_cond_destroy((pthread_cond_t *)c); }

int tss_create(tss_t *key, tss_dtor_t dtor) {
  return pthread_key_create(key, dtor) == 0 ? thrd_success : thrd_error;
}

void tss_delete(tss_t key) { pthread_key_delete(key); }

void *tss_get(tss_t key) { return pthread_getspecific(key); }

int tss_set(tss_t key, void *value) {
  return pthread_setspecific(key, value) == 0 ? thrd_success : thrd_error;
}
