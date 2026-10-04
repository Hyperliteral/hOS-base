#include <setjmp.h>

// Register save area layout (psABI callee-saved set, musl-compatible order):
// [0]=rbx [8]=rbp [16]=r12 [24]=r13 [32]=r14 [40]=r15 [48]=rsp [56]=rip.
// Naked: no compiler prologue/epilogue, so rdi is exactly env and the stack
// holds exactly our return address. returns_twice keeps callers correct.
__attribute__((naked, returns_twice)) int setjmp(jmp_buf env) {
  __asm__ __volatile__("movq %rbx, 0(%rdi)\n\t"
                       "movq %rbp, 8(%rdi)\n\t"
                       "movq %r12, 16(%rdi)\n\t"
                       "movq %r13, 24(%rdi)\n\t"
                       "movq %r14, 32(%rdi)\n\t"
                       "movq %r15, 40(%rdi)\n\t"
                       "leaq 8(%rsp), %rax\n\t"
                       "movq %rax, 48(%rdi)\n\t"
                       "movq (%rsp), %rax\n\t"
                       "movq %rax, 56(%rdi)\n\t"
                       "xorl %eax, %eax\n\t"
                       "retq");
}
