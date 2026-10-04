// atfork handlers + misc pthread helpers. fork() (src/unistd/fork.c) runs
// prepare in the parent before cloning, parent/child after; the heap lock is
// rebalanced so a child of a threaded parent never inherits a locked heap.
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include "internal/heap.h"
#include "internal/pthread_impl.h"
#include "internal/syscall.h"

#define ATFORK_MAX 32

static struct {
  void (*prepare)(void);
  void (*parent)(void);
  void (*child)(void);
} atfork_list[ATFORK_MAX];
static int atfork_count;

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void)) {
  if (atfork_count >= ATFORK_MAX)
    return ENOMEM;
  atfork_list[atfork_count].prepare = prepare;
  atfork_list[atfork_count].parent = parent;
  atfork_list[atfork_count].child = child;
  atfork_count++;
  return 0;
}

void __cavos_atfork_prepare(void) {
  for (int i = atfork_count - 1; i >= 0; i--)
    if (atfork_list[i].prepare)
      atfork_list[i].prepare();
  __cavos_heap_lock();
}

void __cavos_atfork_parent(void) {
  __cavos_heap_unlock();
  for (int i = 0; i < atfork_count; i++)
    if (atfork_list[i].parent)
      atfork_list[i].parent();
}

void __cavos_atfork_child(void) {
  __cavos_heap_unlock();
  for (int i = 0; i < atfork_count; i++)
    if (atfork_list[i].child)
      atfork_list[i].child();
}

// --- sched_yield + scheduler stubs ------------------------------------------
int sched_yield(void) {
  return (int)__cavos_ret((unsigned long)__syscall0(SYS_sched_yield));
}

int sched_get_priority_max(int policy) {
  (void)policy;
  return 0;
}

int sched_get_priority_min(int policy) {
  (void)policy;
  return 0;
}

int sched_getparam(pid_t pid, struct sched_param *param) {
  (void)pid;
  if (!param)
    return -1;
  param->sched_priority = 0;
  return 0;
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param) {
  (void)pid;
  (void)policy;
  (void)param;
  *__errno_location() = ENOSYS;
  return -1;
}

int sched_setparam(pid_t pid, const struct sched_param *param) {
  (void)pid;
  (void)param;
  *__errno_location() = ENOSYS;
  return -1;
}

int sched_getscheduler(pid_t pid) {
  (void)pid;
  return SCHED_OTHER;
}

int sched_rr_get_interval(pid_t pid, struct timespec *tp) {
  (void)pid;
  if (!tp)
    return -1;
  tp->tv_sec = 0;
  tp->tv_nsec = 0;
  return 0;
}

// --- affinity/concurrency/name stubs -----------------------------------------
int pthread_getconcurrency(void) { return 0; }

int pthread_setconcurrency(int n) {
  (void)n;
  return 0;
}

int pthread_getcpuclockid(pthread_t th, clockid_t *clock) {
  (void)th;
  (void)clock;
  return ENOSYS;
}

int pthread_getschedparam(pthread_t th, int *__restrict policy,
                          struct sched_param *__restrict param) {
  (void)th;
  if (!policy || !param)
    return EINVAL;
  *policy = SCHED_OTHER;
  param->sched_priority = 0;
  return 0;
}

int pthread_setschedparam(pthread_t th, int policy,
                          const struct sched_param *param) {
  (void)th;
  (void)policy;
  (void)param;
  return ENOSYS;
}

int pthread_setschedprio(pthread_t th, int prio) {
  (void)th;
  (void)prio;
  return ENOSYS;
}
