// The kernel consumes struct k_sigaction, not the user struct sigaction:
// handler, flags with SA_RESTORER, a restorer trampoline, and the mask.
// Passing the user struct straight through misinterprets its fields and the
// first delivered signal (e.g. bash's SIGCHLD after a pipeline) jumps to
// garbage and dies. This mirrors musl's __libc_sigaction translation.
#include <errno.h>
#include <signal.h>
#include <string.h>

#include "internal/syscall.h"

struct k_sigaction {
  void (*handler)(int);
  unsigned long flags;
  void (*restorer)(void);
  unsigned mask[2];
};

// rt_sigreturn trampoline the kernel returns through after a handler runs.
// Pure syscall, never returns; naked so no prologue frame is emitted.
__attribute__((naked)) static void __restore_rt(void) {
  __asm__ __volatile__("mov $15, %rax\n\tsyscall");
}

int sigaction(int sig, const struct sigaction *restrict action,
              struct sigaction *restrict old) {
  struct k_sigaction ksa, ksa_old;
  if (sig - 32U < 3 || (unsigned)sig - 1U >= (unsigned)(_NSIG - 1)) {
    errno = EINVAL;
    return -1;
  }
  if (action) {
    ksa.handler = action->sa_handler;
    ksa.flags = (unsigned long)action->sa_flags | SA_RESTORER;
    ksa.restorer = __restore_rt;
    memcpy(&ksa.mask, &action->sa_mask, sizeof(ksa.mask));
  }
  long r = __cavos_ret((unsigned long)__syscall4(
      SYS_rt_sigaction, sig, (long)(action ? &ksa : 0),
      (long)(old ? &ksa_old : 0), _NSIG / 8));
  if (r == 0 && old) {
    old->sa_handler = ksa_old.handler;
    old->sa_flags = (int)(ksa_old.flags & ~SA_RESTORER);
    memcpy(&old->sa_mask, &ksa_old.mask, sizeof(ksa_old.mask));
  }
  return (int)r;
}
