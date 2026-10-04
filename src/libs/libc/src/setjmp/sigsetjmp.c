#include <setjmp.h>
#include <signal.h>

#include "internal/syscall.h"

// Replicates musl's sigsetjmp wrapper protocol exactly (see
// musl-1.2.5/src/signal/x86_64/sigsetjmp.s), because a plain C call to
// setjmp would save this wrapper's dead frame instead of the caller's:
//   savemask==0: pure tail jump to setjmp, zero wrapper residue.
//   savemask!=0: pop our return address aside (so setjmp sees the caller's
//     frame), call setjmp, push the address back, and tail into the tail
//     below - which saves the mask on the direct return path and restores
//     it on the longjmp return path. rbx is spilled into the buffer across
//     the setjmp call for the same reason.
__attribute__((naked)) int sigsetjmp(sigjmp_buf env __attribute__((unused)), int savemask __attribute__((unused))) {
  __asm__ __volatile__("testl %esi, %esi\n\t"
                       "jz 1f\n\t"
                       "popq 64(%rdi)\n\t"
                       "movq %rbx, 80(%rdi)\n\t"
                       "movq %rdi, %rbx\n\t"
#ifdef __APPLE__
                       "call _setjmp\n\t"
#else
                       "call setjmp\n\t"
#endif
                       "pushq 64(%rbx)\n\t"
                       "movq %rbx, %rdi\n\t"
                       "movl %eax, %esi\n\t"
                       "movq 80(%rbx), %rbx\n\t"
#ifdef __APPLE__
                       "jmp ___sigsetjmp_tail\n\t"
#else
                       "jmp __sigsetjmp_tail\n\t"
#endif
                       "1:\n\t"
#ifdef __APPLE__
                       "jmp _setjmp\n\t");
#else
                       "jmp setjmp\n\t");
#endif
}

// The second half of the protocol above. ret==0 is the direct call (save
// the current mask); ret!=0 is a siglongjmp resume (restore it).
int __sigsetjmp_tail(sigjmp_buf env, int ret) {
  void *mask = env->__ss;
  __syscall4(SYS_rt_sigprocmask, SIG_SETMASK, ret ? (long)mask : 0,
             ret ? 0 : (long)mask, _NSIG / 8);
  return ret;
}
