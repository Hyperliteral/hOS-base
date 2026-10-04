#include <setjmp.h>

// Restores the area setjmp saved (see setjmp.c for the layout) and resumes
// the caller with value (0 arrives as 1, per the standard). Naked, so the
// ABI registers are exactly env/value with no compiler interference.
__attribute__((naked, noreturn)) void longjmp(jmp_buf env, int value) {
  __asm__ __volatile__("movl %esi, %eax\n\t"
                       "testl %eax, %eax\n\t"
                       "jnz 1f\n\t"
                       "movl $1, %eax\n\t"
                       "1:\n\t"
                       "movq 0(%rdi), %rbx\n\t"
                       "movq 8(%rdi), %rbp\n\t"
                       "movq 16(%rdi), %r12\n\t"
                       "movq 24(%rdi), %r13\n\t"
                       "movq 32(%rdi), %r14\n\t"
                       "movq 40(%rdi), %r15\n\t"
                       "movq 48(%rdi), %rsp\n\t"
                       "jmpq *56(%rdi)\n\t"
                       ".text\n");
}
