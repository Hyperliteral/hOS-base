// Private thread runtime: TCB layout, futex/once/key helpers, TLS plumbing.
//
// POSIX surface lives in <pthread.h>/<threads.h> (vendored musl headers);
// everything here is the implementation behind it, shared by the ELF (.so)
// and Mach-O (.dylib) halves: raw `syscall` encoding is identical and the
// syscall numbers match the kernel's Linux-compatible table on both paths.
//
// TCB contract (x86_64 psABI):
//   TP (%fs base) points at struct __pthread itself, self is at +0x00 and
//   the stack canary at +0x28 (the compiler hard-codes %fs:0x28, so the
//   _Static_assert below is load-bearing, not decorative).
#ifndef CAVOS_INTERNAL_PTHREAD_H
#define CAVOS_INTERNAL_PTHREAD_H

#include <stddef.h>
#include <stdint.h>

#include <limits.h>
#include <sched.h>
#include <time.h>

// Linux futex op numbers. The kernel strips FUTEX_PRIVATE_FLAG, so _PRIVATE
// and shared ops behave identically there; pass _PRIVATE like musl does.
#define CAVOS_FUTEX_WAIT 0
#define CAVOS_FUTEX_WAKE 1
#define CAVOS_FUTEX_REQUEUE 3
#define CAVOS_FUTEX_PRIVATE_FLAG 128
#define CAVOS_FUTEX_WAIT_PRIVATE (CAVOS_FUTEX_WAIT | CAVOS_FUTEX_PRIVATE_FLAG)
#define CAVOS_FUTEX_WAKE_PRIVATE (CAVOS_FUTEX_WAKE | CAVOS_FUTEX_PRIVATE_FLAG)

// detach_state values. EXITED threads leave the word for a joiner to reap;
// detached threads free themselves.
#define CAVOS_DT_JOINABLE 0
#define CAVOS_DT_DETACHED 1
#define CAVOS_DT_EXITED 2

// Default thread stack when the attribute does not specify one.
#define CAVOS_DEFAULT_STACK_SIZE (1024 * 1024)

struct __pthread {
  struct __pthread *self;      // +0x00: TP points here
  int               tid;       // +0x08: kernel task id (gettid)
  int               errno_val; // +0x0c: thread-local errno
  volatile int detach_state;   // +0x10: JOINABLE/DETACHED/EXITED, futex word
  int               _pad;      // +0x14
  void             *stack_base; // +0x18: mmap base, 0 for the main thread
  size_t            stack_size;  // +0x20
  uintptr_t         canary;      // +0x28: %fs:0x28 stack canary
  void *(*start)(void *);       // +0x30
  void             *arg;        // +0x38
  void             *result;     // +0x40
  int               cancel_state;   // +0x48: 0 enabled, 1 disabled
  int               cancel_type;    // +0x4c: 0 deferred, 1 async (async=ENOTSUP)
  volatile int      cancel_pending; // +0x50
  int               tsd_used;       // +0x54: any TSD values set (destructor pass)
  void *tsd[PTHREAD_KEYS_MAX];      // +0x58
  unsigned tsd_gen[PTHREAD_KEYS_MAX]; // key generation per slot
  void            **dtv;             // DTV for __tls_get_addr (shared table)
  size_t            dtv_len;
  int               own_stack; // stack mmap'd here (freed on join/exit)
};

_Static_assert(__builtin_offsetof(struct __pthread, canary) == 0x28,
               "TCB canary must sit at %fs:0x28");

static __inline struct __pthread *__cavos_self(void) {
  // Uniform TP read for both targets: __builtin_thread_pointer() does not
  // exist on the Mach-O target (no darwin TLS backend in this clang), and
  // on ELF it lowers to exactly this instruction. Reads the self word at
  // %fs:0, which every setup path initializes to the TCB address.
  struct __pthread *t;
  __asm__ __volatile__("mov %%fs:0, %0" : "=r"(t));
  return t;
}

// --- futex ---------------------------------------------------------------
long __cavos_futex(long *addr, int op, int val,
                   const struct timespec *timeout);

static __inline int __cavos_futex_wait(volatile int *addr, int val,
                                       const struct timespec *relto) {
  return (int)__cavos_futex((long *)addr, CAVOS_FUTEX_WAIT_PRIVATE, val, relto);
}

static __inline int __cavos_futex_wake(volatile int *addr, int n) {
  return (int)__cavos_futex((long *)addr, CAVOS_FUTEX_WAKE_PRIVATE, n, 0);
}

// --- threads -------------------------------------------------------------
long __cavos_clone(unsigned long flags, void *stack, int *ptid, void *ctid,
                   void *tls);
// Exit the current thread, freeing nothing: the joiner reaps a joinable
// thread; a detached thread unmaps itself without touching the stack.
void __cavos_unmap_and_exit(void *base, size_t size);
// Child-side entry: reads everything through TP, never returns.
_Noreturn void __cavos_thread_entry(void);
// Main-thread TCB + optional static TLS image (exe PHDRs from auxv).
// phdr/phnum/base describe the main executable; a NULL phdr installs a
// bare TCB (Mach-O path: no compiler TLS exists for that target).
void __cavos_init_main_tls(const void *phdr, size_t phnum, uintptr_t base);
// Register one PT_TLS template for static TLS (__tls_get_addr). Called at
// startup for the exe and each initially-loaded library, before threads
// exist. Returns the assigned module id, or -1 when the table is full.
long __cavos_tls_register(uintptr_t addr, size_t memsz, size_t filesz,
                          size_t align);
void __cavos_tls_register_image(const void *phdr, size_t phnum,
                                uintptr_t bias);
void __cavos_install_main_tcb(void);
// Per-thread static TLS area carved from the new thread's stack mapping.
void __cavos_tls_init_thread(struct __pthread *t, void *static_base,
                             size_t static_size);

// --- atfork ----------------------------------------------------------------
void __cavos_atfork_prepare(void);
void __cavos_atfork_parent(void);
void __cavos_atfork_child(void);

// --- heap lock (spin; leaf-level around freelist ops only) ------------------
void __cavos_heap_lock(void);
void __cavos_heap_unlock(void);

// --- time helper: abstime(CLOCK_REALTIME) -> relative ----------------------
int __cavos_abstime_to_rel(const struct timespec *abstime,
                           struct timespec *rel);

// --- deferred cancellation checkpoint --------------------------------------
void __cavos_testcancel(void);

#endif
