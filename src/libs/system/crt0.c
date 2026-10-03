#include "system.h"

// musl's process entry point. Declared here rather than pulled from a musl
// header because musl marks it hidden, but it is a global symbol in libc.a and
// so links fine against the static libc we build against.
extern int __libc_start_main(int (*main)(int, char **, char **), int argc,
                             char **argv, void (*init)(void),
                             void (*fini)(void), void (*ldso_dummy)(void));

extern void _init(void);
extern void _fini(void);

extern int main(int argc, char *argv[], char *envp[]);

void         cavosStartMain(uint64_t stack);

// We are entered by a jump rather than a call, so nothing guarantees the
// alignment of the stack the kernel handed us: it depends on how many words the
// kernel pushed for argv/envp/auxv. Save the raw stack pointer first (the
// kernel-built frame is what we have to parse), then align before the call so
// that cavosStartMain is entered exactly the way any called function is: with
// RSP % 16 == 8, giving the ABI-mandated alignment to main and everything under
// it. "and" leaves RSP 16-byte aligned and the call then pushes the return
// address. Zeroing %rbp marks this as the outermost frame, matching musl's own
// crt1, so a backtrace walking the frame chain terminates here instead of
// wandering into whatever the kernel left below the frame.
__attribute__((naked, noreturn)) void _start(void) {
  __asm__ __volatile__("xor %rbp, %rbp\n\t"
                       "movq %rsp, %rdi\n\t"
                       "andq $-16, %rsp\n\t"
                       "call cavosStartMain\n\t"
                       "ud2");
}

// musl's libc is not usable the instant the kernel starts us: errno, every
// __pthread_self() and all of musl's thread-local state are reached through the
// %fs base, which nothing sets until musl's own startup has run. Calling main()
// directly therefore left %fs at 0, so the first errno access - which is what
// musl's mount() does when it reports ENOSYS - read linear address 0 and took a
// page fault. Hand off to __libc_start_main instead, exactly as musl's crt1
// does: it runs __init_libc/__init_tls (which issues arch_prctl(ARCH_SET_FS)
// through our syscall), runs the init/fini arrays, and only then calls main.
// It ends in exit(), so control never comes back.
void cavosStartMain(uint64_t stack) {
  long   *values = (long *)stack;
  int     argc = (int)values[0];
  char  **argv = (char **)(values + 1);

  __libc_start_main(main, argc, argv, _init, _fini, 0);
}
