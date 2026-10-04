// Deferred cancellation only. pthread_cancel marks the target; the request
// is acted on at cancellation points (cond/mutex-timed waits, sleep,
// blocking I/O wrappers do not exist yet) and by pthread_testcancel.
// Asynchronous mode is rejected: without compiler unwinding tables and
// kernel-side restart-block support it cannot be done safely. Cancellation
// of a thread blocked in the kernel relies on signals the kernel does not
// deliver cross-thread (no tgkill), so a cancel may wait until the target
// reaches a userspace checkpoint - documented, POSIX-deferred subset.
#include <errno.h>
#include <pthread.h>

#include "internal/pthread_impl.h"

#define PTHREAD_CANCELED_VALUE ((void *)-1)

int pthread_setcancelstate(int state, int *old) {
  if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
    return EINVAL;
  struct __pthread *t = __cavos_self();
  if (old)
    *old = t ? t->cancel_state : PTHREAD_CANCEL_ENABLE;
  if (t)
    t->cancel_state = state;
  return 0;
}

int pthread_setcanceltype(int type, int *old) {
  if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
    return EINVAL;
  if (type == PTHREAD_CANCEL_ASYNCHRONOUS)
    return ENOTSUP;
  struct __pthread *t = __cavos_self();
  if (old)
    *old = t ? t->cancel_type : PTHREAD_CANCEL_DEFERRED;
  if (t)
    t->cancel_type = type;
  return 0;
}

void __cavos_testcancel(void) {
  struct __pthread *t = __cavos_self();
  if (t && t->cancel_state == PTHREAD_CANCEL_ENABLE &&
      __atomic_load_n(&t->cancel_pending, __ATOMIC_ACQUIRE))
    pthread_exit(PTHREAD_CANCELED_VALUE);
}

void pthread_testcancel(void) { __cavos_testcancel(); }

int pthread_cancel(pthread_t th) {
  struct __pthread *t = (struct __pthread *)(uintptr_t)th;
  if (!t || t->self != t)
    return ESRCH;
  __atomic_store_n(&t->cancel_pending, 1, __ATOMIC_RELEASE);
  return 0;
}

void _pthread_cleanup_push(struct __ptcb *cb, void (*f)(void *), void *x) {
  cb->__f = f;
  cb->__x = x;
  cb->__next = 0;
}

void _pthread_cleanup_pop(struct __ptcb *cb, int run) {
  if (run)
    cb->__f(cb->__x);
}
