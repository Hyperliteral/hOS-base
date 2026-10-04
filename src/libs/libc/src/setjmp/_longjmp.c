#include <setjmp.h>

__attribute__((naked, noreturn)) void _longjmp(jmp_buf env __attribute__((unused)), int value __attribute__((unused))) {
#ifdef __APPLE__
  __asm__ __volatile__("jmp _longjmp");
#else
  __asm__ __volatile__("jmp longjmp");
#endif
}
