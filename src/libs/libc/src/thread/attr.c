// Attribute objects with musl's packing (see create.c): stacksize=__s[0],
// guardsize=__s[1], stack-top=__s[2], detach=__i[6], sched=__i[7],
// policy=__i[8], prio=__i[9], inheritsched=__i[10]. Scheduling policy is
// accepted and stored (the kernel runs one class), scope is SYSTEM-only.
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <limits.h>

#include "internal/pthread_impl.h"

#define A_STACKSIZE(a) ((a)->__u.__s[0])
#define A_GUARDSIZE(a) ((a)->__u.__s[1])
#define A_STACKTOP(a) ((a)->__u.__s[2])
#define A_DETACH(a) ((a)->__u.__i[6])
#define A_SCHED(a) ((a)->__u.__i[7])
#define A_POLICY(a) ((a)->__u.__i[8])
#define A_PRIO(a) ((a)->__u.__i[9])
#define A_INHERIT(a) ((a)->__u.__i[10])

int pthread_attr_init(pthread_attr_t *a) {
  if (!a)
    return EINVAL;
  *a = (pthread_attr_t){0};
  A_STACKSIZE(a) = 128 * 1024;
  A_GUARDSIZE(a) = 8192;
  A_INHERIT(a) = PTHREAD_INHERIT_SCHED;
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *a) {
  if (!a)
    return EINVAL;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *a, int state) {
  if (!a || (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED))
    return EINVAL;
  A_DETACH(a) = state;
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *a, int *state) {
  if (!a || !state)
    return EINVAL;
  *state = A_DETACH(a);
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *a, size_t size) {
  if (!a || size < PTHREAD_STACK_MIN)
    return EINVAL;
  A_STACKSIZE(a) = size;
  return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *__restrict a,
                              size_t *__restrict size) {
  if (!a || !size)
    return EINVAL;
  *size = A_STACKSIZE(a) ? (size_t)A_STACKSIZE(a) : CAVOS_DEFAULT_STACK_SIZE;
  return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *a, size_t size) {
  if (!a)
    return EINVAL;
  A_GUARDSIZE(a) = size;
  return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t *__restrict a,
                              size_t *__restrict size) {
  if (!a || !size)
    return EINVAL;
  *size = (size_t)A_GUARDSIZE(a);
  return 0;
}

int pthread_attr_setstack(pthread_attr_t *a, void *addr, size_t size) {
  if (!a || !addr || size < PTHREAD_STACK_MIN)
    return EINVAL;
  A_STACKTOP(a) = (unsigned long)((char *)addr + size);
  A_STACKSIZE(a) = size;
  return 0;
}

int pthread_attr_getstack(const pthread_attr_t *__restrict a,
                          void **__restrict addr, size_t *__restrict size) {
  if (!a || !addr || !size)
    return EINVAL;
  if (!A_STACKTOP(a))
    return EINVAL;
  *size = (size_t)A_STACKSIZE(a);
  *addr = (void *)((uintptr_t)A_STACKTOP(a) - *size);
  return 0;
}

int pthread_attr_setscope(pthread_attr_t *a, int scope) {
  if (!a || scope != PTHREAD_SCOPE_SYSTEM)
    return EINVAL;
  return 0;
}

int pthread_attr_getscope(const pthread_attr_t *__restrict a,
                          int *__restrict scope) {
  if (!a || !scope)
    return EINVAL;
  *scope = PTHREAD_SCOPE_SYSTEM;
  return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *a, int policy) {
  if (!a || (policy != SCHED_OTHER && policy != SCHED_FIFO &&
             policy != SCHED_RR && policy != SCHED_BATCH &&
             policy != SCHED_IDLE))
    return EINVAL;
  A_POLICY(a) = policy;
  return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t *__restrict a,
                                int *__restrict policy) {
  if (!a || !policy)
    return EINVAL;
  *policy = A_POLICY(a);
  return 0;
}

int pthread_attr_setschedparam(pthread_attr_t *__restrict a,
                               const struct sched_param *__restrict param) {
  if (!a || !param)
    return EINVAL;
  A_PRIO(a) = param->sched_priority;
  return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t *__restrict a,
                               struct sched_param *__restrict param) {
  if (!a || !param)
    return EINVAL;
  param->sched_priority = A_PRIO(a);
  return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t *a, int inherit) {
  if (!a || (inherit != PTHREAD_INHERIT_SCHED && inherit != PTHREAD_EXPLICIT_SCHED))
    return EINVAL;
  A_INHERIT(a) = inherit;
  return 0;
}

int pthread_attr_getinheritsched(const pthread_attr_t *__restrict a,
                                 int *__restrict inherit) {
  if (!a || !inherit)
    return EINVAL;
  *inherit = A_INHERIT(a);
  return 0;
}

// --- mutex attrs (type/robust/proto/shared packed like musl: low 4 bits
// type, next 4 robust, next 4 proto, then pshared) ---------------------------
int pthread_mutexattr_init(pthread_mutexattr_t *a) {
  if (!a)
    return EINVAL;
  a->__attr = 0;
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
  if (!a)
    return EINVAL;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type) {
  if (!a || type < 0 || type > 2)
    return EINVAL;
  a->__attr = (a->__attr & ~15) | (unsigned)type;
  return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict a,
                              int *__restrict type) {
  if (!a || !type)
    return EINVAL;
  *type = a->__attr & 15;
  return 0;
}

int pthread_mutexattr_setrobust(pthread_mutexattr_t *a, int robust) {
  if (!a || (robust != PTHREAD_MUTEX_STALLED && robust != PTHREAD_MUTEX_ROBUST))
    return EINVAL;
  a->__attr = (a->__attr & ~(3u << 4)) | ((unsigned)robust << 4);
  return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict a,
                                int *__restrict robust) {
  if (!a || !robust)
    return EINVAL;
  *robust = (a->__attr >> 4) & 3;
  return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int proto) {
  if (!a || proto < 0 || proto > 2)
    return EINVAL;
  a->__attr = (a->__attr & ~(3u << 8)) | ((unsigned)proto << 8);
  return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *__restrict a,
                                  int *__restrict proto) {
  if (!a || !proto)
    return EINVAL;
  *proto = (a->__attr >> 8) & 3;
  return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int shared) {
  if (!a || (shared != PTHREAD_PROCESS_PRIVATE && shared != PTHREAD_PROCESS_SHARED))
    return EINVAL;
  return 0; // no distinction exists; accept both
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *__restrict a,
                                int *__restrict shared) {
  if (!a || !shared)
    return EINVAL;
  *shared = PTHREAD_PROCESS_PRIVATE;
  return 0;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int prio) {
  (void)a;
  (void)prio;
  return ENOTSUP;
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *__restrict a,
                                     int *__restrict prio) {
  (void)a;
  (void)prio;
  return ENOTSUP;
}

// --- cond attrs (clock in low 16, pshared above; mirrors cond.c) -------------
int pthread_condattr_init(pthread_condattr_t *a) {
  if (!a)
    return EINVAL;
  a->__attr = 0; // CLOCK_REALTIME, private
  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *a) {
  if (!a)
    return EINVAL;
  return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t clock) {
  if (!a || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC))
    return EINVAL;
  a->__attr = (a->__attr & ~0xffffu) | (unsigned)clock;
  return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *__restrict a,
                              clockid_t *__restrict clock) {
  if (!a || !clock)
    return EINVAL;
  *clock = a->__attr & 0xffff;
  return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int shared) {
  if (!a || (shared != PTHREAD_PROCESS_PRIVATE && shared != PTHREAD_PROCESS_SHARED))
    return EINVAL;
  return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t *__restrict a,
                               int *__restrict shared) {
  if (!a || !shared)
    return EINVAL;
  *shared = PTHREAD_PROCESS_PRIVATE;
  return 0;
}

// --- rwlock/barrier attrs: pshared only --------------------------------------
int pthread_rwlockattr_init(pthread_rwlockattr_t *a) {
  if (!a)
    return EINVAL;
  a->__attr[0] = a->__attr[1] = 0;
  return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a) {
  if (!a)
    return EINVAL;
  return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int shared) {
  if (!a || (shared != PTHREAD_PROCESS_PRIVATE && shared != PTHREAD_PROCESS_SHARED))
    return EINVAL;
  return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *__restrict a,
                                  int *__restrict shared) {
  if (!a || !shared)
    return EINVAL;
  *shared = PTHREAD_PROCESS_PRIVATE;
  return 0;
}

int pthread_barrierattr_init(pthread_barrierattr_t *a) {
  if (!a)
    return EINVAL;
  a->__attr = 0;
  return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *a) {
  if (!a)
    return EINVAL;
  return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int shared) {
  if (!a || (shared != PTHREAD_PROCESS_PRIVATE && shared != PTHREAD_PROCESS_SHARED))
    return EINVAL;
  return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *__restrict a,
                                   int *__restrict shared) {
  if (!a || !shared)
    return EINVAL;
  *shared = PTHREAD_PROCESS_PRIVATE;
  return 0;
}
