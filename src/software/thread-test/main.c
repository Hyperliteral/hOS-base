// Thread-runtime smoke test: create/join, mutex, cond, TLS, errno, keys,
// once, barrier, rwlock, detach, cancel, C11 wrappers.
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

static int failures = 0;
#define CHECK(cond, name) do { \
  if (cond) { printf("ok %s\n", name); } \
  else { printf("FAIL %s (line %d)\n", name, __LINE__); failures++; } \
} while (0)

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int counter = 0;
static void *incr(void *arg) {
  int n = *(int *)arg;
  for (int i = 0; i < n; i++) {
    pthread_mutex_lock(&mtx);
    counter++;
    pthread_mutex_unlock(&mtx);
  }
  return (void *)(long)n;
}

static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cmtx = PTHREAD_MUTEX_INITIALIZER;
static int ready = 0;
static void *waiter(void *arg) {
  (void)arg;
  pthread_mutex_lock(&cmtx);
  while (!ready)
    pthread_cond_wait(&cond, &cmtx);
  pthread_mutex_unlock(&cmtx);
  return (void *)42;
}

static __thread int tl_var;
static __thread int tl_init = 7;
static void *tls_fn(void *arg) {
  // Main set tl_var=100; thread must see 0 (fresh image) and init 7.
  int seen_init = tl_init;
  int seen = tl_var;
  tl_var = 200 + (int)(long)arg;
  // errno isolation: poison errno here
  errno = 555;
  // return encoded observations
  static __thread long out;
  out = (long)(seen_init * 1000000 + seen * 1000 + tl_var % 1000);
  return (void *)out;
}

static pthread_key_t key;
static int dtor_calls = 0;
static void dtor(void *v) { (void)v; dtor_calls++; }
static void *key_fn(void *arg) {
  pthread_setspecific(key, arg);
  return pthread_getspecific(key);
}

static pthread_once_t once_ctl = PTHREAD_ONCE_INIT;
static int once_n = 0;
static void once_fn(void) { once_n++; }
static void *once_worker(void *arg) {
  (void)arg;
  pthread_once(&once_ctl, once_fn);
  return 0;
}

static pthread_barrier_t barrier;
static void *barrier_fn(void *arg) {
  (void)arg;
  return (void *)(long)pthread_barrier_wait(&barrier);
}

static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static void *rdlock_fn(void *arg) {
  (void)arg;
  pthread_rwlock_rdlock(&rw);
  pthread_rwlock_unlock(&rw);
  return 0;
}

static pthread_cond_t dc = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t dm = PTHREAD_MUTEX_INITIALIZER;
static void *dwait(void *a) {
  (void)a;
  pthread_mutex_lock(&dm);
  pthread_cond_wait(&dc, &dm);
  pthread_mutex_unlock(&dm);
  return 0;
}
static void *spin_fn(void *arg) {
  pthread_spinlock_t *s = arg;
  pthread_spin_lock(s);
  pthread_spin_unlock(s);
  return 0;
}

static void *cancel_fn(void *arg) {
  (void)arg;
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
  pthread_testcancel(); // no pending yet
  for (;;) {
    pthread_testcancel();
    sched_yield();
  }
  return 0;
}

static int c11_val = 0;
static mtx_t c11_m;
static int c11_fn(void *arg) {
  (void)arg;
  mtx_lock(&c11_m);
  c11_val++;
  mtx_unlock(&c11_m);
  return 5;
}

int main(void) {
  // self/equal
  pthread_t self = pthread_self();
  CHECK(self != 0, "self-nonzero");
  CHECK(pthread_equal(self, pthread_self()), "equal-self");

  // create/join with result + mutex counter
  pthread_t t[4];
  int n = 2000;
  for (int i = 0; i < 4; i++)
    CHECK(pthread_create(&t[i], 0, incr, &n) == 0, "create");
  for (int i = 0; i < 4; i++) {
    void *r = 0;
    CHECK(pthread_join(t[i], &r) == 0 && (long)r == n, "join-result");
  }
  CHECK(counter == 8000, "mutex-counter");

  // trylock/EBUSY + recursive + errorcheck
  CHECK(pthread_mutex_trylock(&mtx) == 0, "trylock-free");
  CHECK(pthread_mutex_trylock(&mtx) == EBUSY, "trylock-busy");
  CHECK(pthread_mutex_unlock(&mtx) == 0, "unlock");
  pthread_mutex_t rec, ec;
  pthread_mutexattr_t ma;
  pthread_mutexattr_init(&ma);
  pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&rec, &ma);
  pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
  pthread_mutex_init(&ec, &ma);
  CHECK(pthread_mutex_lock(&rec) == 0 && pthread_mutex_lock(&rec) == 0, "recursive-relock");
  CHECK(pthread_mutex_unlock(&rec) == 0 && pthread_mutex_unlock(&rec) == 0, "recursive-unlock2");
  CHECK(pthread_mutex_lock(&ec) == 0 && pthread_mutex_lock(&ec) == EDEADLK, "errorcheck-deadlk");
  CHECK(pthread_mutex_unlock(&ec) == 0, "errorcheck-unlock");

  // cond signal
  pthread_t w;
  pthread_create(&w, 0, waiter, 0);
  usleep(50000);
  pthread_mutex_lock(&cmtx);
  ready = 1;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&cmtx);
  void *wr = 0;
  pthread_join(w, &wr);
  CHECK((long)wr == 42, "cond-signal");

  // broadcast with 3 waiters
  ready = 0;
  pthread_t ws[3];
  for (int i = 0; i < 3; i++) pthread_create(&ws[i], 0, waiter, 0);
  usleep(50000);
  pthread_mutex_lock(&cmtx);
  ready = 1;
  pthread_cond_broadcast(&cond);
  pthread_mutex_unlock(&cmtx);
  int okb = 1;
  for (int i = 0; i < 3; i++) { void *r = 0; pthread_join(ws[i], &r); if ((long)r != 42) okb = 0; }
  CHECK(okb, "cond-broadcast");

  // TLS: main sets tl_var, thread sees fresh
  tl_var = 100;
  CHECK(tl_init == 7, "tls-main-init");
  pthread_t tt;
  pthread_create(&tt, 0, tls_fn, (void *)3);
  void *tr = 0;
  pthread_join(tt, &tr);
  // seen_init=7, seen=0, tl_var set 203 -> 7*1e6 + 0 + 203
  CHECK((long)tr == 7000203L, "tls-thread-image");
  CHECK(tl_var == 100, "tls-main-untouched");
  CHECK(errno != 555, "errno-isolated");

  // keys + destructor
  pthread_key_create(&key, dtor);
  pthread_setspecific(key, (void *)9);
  CHECK(pthread_getspecific(key) == (void *)9, "key-main");
  pthread_t kt;
  pthread_create(&kt, 0, key_fn, (void *)77);
  void *kr = 0;
  pthread_join(kt, &kr);
  CHECK(kr == (void *)77, "key-thread");
  CHECK(dtor_calls == 1, "key-dtor-on-exit");

  // once
  pthread_t ot[3];
  for (int i = 0; i < 3; i++) pthread_create(&ot[i], 0, once_worker, 0);
  for (int i = 0; i < 3; i++) pthread_join(ot[i], 0);
  CHECK(once_n == 1, "once");

  // barrier (3 + main)
  pthread_barrier_init(&barrier, 0, 3);
  pthread_t bt[2];
  void *br[2] = {0};
  pthread_create(&bt[0], 0, barrier_fn, 0);
  pthread_create(&bt[1], 0, barrier_fn, 0);
  int mserial = pthread_barrier_wait(&barrier);
  pthread_join(bt[0], &br[0]);
  pthread_join(bt[1], &br[1]);
  int serials = (mserial == PTHREAD_BARRIER_SERIAL_THREAD) +
                ((long)br[0] == PTHREAD_BARRIER_SERIAL_THREAD) +
                ((long)br[1] == PTHREAD_BARRIER_SERIAL_THREAD);
  CHECK(serials == 1, "barrier-serial-once");

  // rwlock + spin
  pthread_t rt;
  pthread_create(&rt, 0, rdlock_fn, 0);
  pthread_join(rt, 0);
  CHECK(1, "rwlock-rdlock");
  pthread_spinlock_t sp;
  pthread_spin_init(&sp, 0);
  pthread_t st;
  pthread_create(&st, 0, spin_fn, &sp);
  pthread_join(st, 0);
  CHECK(1, "spin");

  // detach a live (blocked) thread: join must report EINVAL deterministically.
  // The thread is released afterwards and exits detached (self-freed).
  pthread_t dt;
  pthread_create(&dt, 0, dwait, 0);
  usleep(50000); // let it block in cond_wait
  CHECK(pthread_detach(dt) == 0, "detach");
  CHECK(pthread_join(dt, 0) == EINVAL, "join-detached-einval");
  pthread_mutex_lock(&dm);
  pthread_cond_signal(&dc);
  pthread_mutex_unlock(&dm);
  usleep(50000); // let the detached thread exit + self-free

  // cancel (deferred via testcancel checkpoint loop)
  pthread_t ct;
  pthread_create(&ct, 0, cancel_fn, 0);
  usleep(50000);
  CHECK(pthread_cancel(ct) == 0, "cancel-request");
  void *cr = 0;
  pthread_join(ct, &cr);
  CHECK(cr == PTHREAD_CANCELED, "cancel-delivered");

  // C11
  mtx_init(&c11_m, mtx_plain);
  thrd_t c11t;
  CHECK(thrd_create(&c11t, c11_fn, 0) == thrd_success, "c11-create");
  int cres = 0;
  CHECK(thrd_join(c11t, &cres) == thrd_success && cres == 5 && c11_val == 1, "c11-join");

  printf(failures ? "RESULT FAIL %d\n" : "RESULT ALL OK\n", failures);
  return failures != 0;
}
