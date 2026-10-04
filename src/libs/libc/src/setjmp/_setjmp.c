#include <setjmp.h>

// Tail jump, not a call: _setjmp must leave no wrapper frame behind, or the
// saved stack would point into dead code on return (same protocol as
// sigsetjmp.c - plain setjmp semantics, no mask involved).
__attribute__((naked)) int _setjmp(jmp_buf env __attribute__((unused))) {
#ifdef __APPLE__
  __asm__ __volatile__("jmp _setjmp");
#else
  __asm__ __volatile__("jmp setjmp");
#endif
}
