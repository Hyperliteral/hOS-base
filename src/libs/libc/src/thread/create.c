// Thread creation, joining and lifecycle.
//
// One clone(2) per thread with the NPTL flag set the kernel supports
// (VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|PARENT_SETTID|CHILD_CLEARTID).
// The child's stack and TCB come from a single mmap'd region: static TLS
// area + TCB at the top, TP installed by the kernel from CLONE_SETTLS.
// Exit clears the tid word and wakes joiners twice over (kernel ctid clear
// plus our detach_state futex); either wake is sufficient, both are cheap.
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "internal/pthread_impl.h"
#include "internal/syscall.h"

extern void __cavos_tls_init_thread(struct __pthread *t, void *static_base,
                                    size_t static_size);
extern size_t __cavos_tls_static_size(void);

// Runs on the child's stack, with TP already set. Reads everything through
// the TCB (no parent-stack access - see src/thread/clone.S), runs TSD
// destructors on the way out, then frees or hands off the stack.
_Noreturn void __cavos_thread_entry(void) {
  struct __pthread *t = __cavos_self();
  __syscall1(SYS_set_tid_address, (long)&t->tid);
  t->result = t->start(t->arg);
  // TSD destructors, up to PTHREAD_DESTRUCTOR_ITERATIONS passes.
  for (int pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; pass++) {
    if (!t->tsd_used)
      break;
    t->tsd_used = 0;
    extern void __cavos_tsd_destroy_pass(struct __pthread * t);
    __cavos_tsd_destroy_pass(t);
  }
  pthread_exit(t->result);
}

// musl attr packing (LP64 __SU=2): stacksize=__s[0], guardsize=__s[1],
// stackaddr(top)=__s[2], detach=__i[6], sched=__i[7], policy=__i[8],
// prio=__i[9]. Mirrored here so attr.o/create.o agree bit-for-bit.
#define A_STACKSIZE(a) ((a)->__u.__s[0])
#define A_GUARDSIZE(a) ((a)->__u.__s[1])
#define A_STACKTOP(a) ((a)->__u.__s[2])
#define A_DETACH(a) ((a)->__u.__i[6])

static size_t attr_stacksize(const pthread_attr_t *attr) {
  if (attr && A_STACKSIZE(attr))
    return (size_t)A_STACKSIZE(attr);
  return CAVOS_DEFAULT_STACK_SIZE;
}

int pthread_create(pthread_t *__restrict res, const pthread_attr_t *__restrict attr,
                   void *(*entry)(void *), void *__restrict arg) {
  if (!res || !entry)
    return EINVAL;
  size_t stack_size = attr_stacksize(attr);
  if (stack_size < PTHREAD_STACK_MIN)
    stack_size = PTHREAD_STACK_MIN;
  stack_size = (stack_size + 4095) & ~(size_t)4095;

  size_t static_size = __cavos_tls_static_size();
  size_t total = stack_size + static_size + sizeof(struct __pthread) + 64;
  total = (total + 4095) & ~(size_t)4095;

  char                *map = 0;
  size_t               map_size = 0;
  struct __pthread *t;
  void                 *static_base;
  if (attr && A_STACKTOP(attr)) {
    // Caller-provided stack: top given, we carve TCB+TLS below it and never
    // free the mapping (caller owns it, exactly like musl).
    char *top = (char *)(uintptr_t)A_STACKTOP(attr);
    uintptr_t taddr =
        ((uintptr_t)top - sizeof(struct __pthread)) & ~(uintptr_t)15;
    t = (struct __pthread *)taddr;
    memset(t, 0, sizeof(*t));
    static_base = (void *)(taddr - static_size);
    t->own_stack = 0;
  } else {
    map = mmap(0, total, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED)
      return ENOMEM;
    map_size = total;
    char *top = map + total;
    // TCB at the very top (16-aligned), static TLS area just below it.
    uintptr_t taddr =
        ((uintptr_t)top - sizeof(struct __pthread)) & ~(uintptr_t)15;
    t = (struct __pthread *)taddr;
    memset(t, 0, sizeof(*t));
    static_base = (void *)(taddr - static_size);
    t->stack_base = map;
    t->stack_size = map_size;
    t->own_stack = 1;
  }
  t->start = entry;
  t->arg = arg;
  t->cancel_state = 0;
  t->cancel_type = 0;
  __cavos_tls_init_thread(t, static_base, static_size);

  unsigned long flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                        CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS |
                        CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;
  // 16-align the child stack top: the kernel hands it over as-is and the
  // entry call needs ABI alignment.
  void *child_sp = (void *)((uintptr_t)static_base & ~(uintptr_t)15);
  long  tid =
      __cavos_clone(flags, child_sp, &t->tid, &t->tid, t);
  if (tid < 0) {
    int e = (int)-tid;
    if (map)
      munmap(map, map_size);
    return e;
  }
  if (attr && A_DETACH(attr)) {
    __atomic_store_n(&t->detach_state, CAVOS_DT_DETACHED,
                     __ATOMIC_RELEASE);
  }
  *res = (pthread_t)(uintptr_t)t;
  return 0;
}

static struct __pthread *to_tcb(pthread_t th) {
  struct __pthread *t = (struct __pthread *)(uintptr_t)th;
  if (!t || t->self != t)
    return 0;
  return t;
}

pthread_t pthread_self(void) { return (pthread_t)(uintptr_t)__cavos_self(); }

// The header macro-ifies pthread_equal for C callers; undef it for the real
// definition (same reason musl's own pthread_equal.c does).
#undef pthread_equal
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

void pthread_exit(void *result) {
  struct __pthread *t = __cavos_self();
  t->result = result;
  for (int pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; pass++) {
    if (!t->tsd_used)
      break;
    t->tsd_used = 0;
    extern void __cavos_tsd_destroy_pass(struct __pthread * t);
    __cavos_tsd_destroy_pass(t);
  }
  // Joinable -> EXITED (joiner reaps); detached -> stays DETACHED so a
  // concurrent join reports EINVAL instead of reaping a freed TCB.
  int expected = CAVOS_DT_JOINABLE;
  int joined = __atomic_compare_exchange_n(
      &t->detach_state, &expected, CAVOS_DT_EXITED, 0, __ATOMIC_ACQ_REL,
      __ATOMIC_ACQUIRE);
  __cavos_futex_wake(&t->detach_state, 1);
  if (joined) {
    __syscall1(SYS_exit, 0);
    for (;;)
      __syscall1(SYS_exit, 0);
  }
  // Detached (or main thread): free our own mapping without touching the
  // stack, then exit. Main has no mapping (base 0) and just exits.
  void  *base = t->stack_base;
  size_t size = t->stack_size;
  if (base && t->own_stack)
    __cavos_unmap_and_exit(base, size);
  __syscall1(SYS_exit, 0);
  for (;;)
    __syscall1(SYS_exit, 0);
}

int pthread_join(pthread_t th, void **result) {
  struct __pthread *t = to_tcb(th);
  if (!t)
    return ESRCH;
  if (t == __cavos_self())
    return EDEADLK;
  for (;;) {
    int st = __atomic_load_n(&t->detach_state, __ATOMIC_ACQUIRE);
    if (st == CAVOS_DT_EXITED)
      break;
    if (st == CAVOS_DT_DETACHED)
      return EINVAL;
    __cavos_futex_wait(&t->detach_state, CAVOS_DT_JOINABLE, 0);
  }
  if (result)
    *result = t->result;
  void  *base = t->stack_base;
  size_t size = t->stack_size;
  // Poison so a second join fails instead of freeing twice.
  t->self = 0;
  if (base)
    munmap(base, size);
  else
    __atomic_store_n(&t->detach_state, CAVOS_DT_DETACHED, __ATOMIC_RELAXED);
  return 0;
}

int pthread_detach(pthread_t th) {
  struct __pthread *t = to_tcb(th);
  if (!t)
    return ESRCH;
  int expected = CAVOS_DT_JOINABLE;
  if (!__atomic_compare_exchange_n(&t->detach_state, &expected,
                                   CAVOS_DT_DETACHED, 0, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE)) {
    return expected == CAVOS_DT_DETACHED ? 0 : EINVAL;
  }
  // A joiner may be parked in futex WAIT on JOINABLE; wake it so it
  // re-checks and reports EINVAL instead of sleeping forever.
  __cavos_futex_wake(&t->detach_state, 1);
  return 0;
}
