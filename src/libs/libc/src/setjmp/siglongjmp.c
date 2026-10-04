#include <setjmp.h>
#include <signal.h>

// Restores the mask saved by sigsetjmp when it was asked to (see sigsetjmp.c
// for the __fl convention), then jumps.
_Noreturn void siglongjmp(sigjmp_buf env, int value) {
  if (env->__fl)
    sigprocmask(SIG_SETMASK, (const sigset_t *)env->__ss, 0);
  longjmp(env, value);
}
