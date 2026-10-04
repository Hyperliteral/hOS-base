#include <stdint.h>

#include "libc.h"

// The Mach-O program entry stub.
//
// The kernel enters here at LC_MAIN's offset with the initial stack laid out as
// argc, argv[], NULL, envp[], NULL. This is the only part of the runtime that
// has to live inside the executable: a dylib cannot supply the entry point,
// because the kernel jumps to an offset in the main image itself.
//
// Nothing else is linked in. environ, main's callees and exit all come from
// libc.dylib, resolved by the loader before _start runs, which makes this file
// the smallest possible exercise of the dynamic linking path - if the loader is
// broken, nothing runs at all.

extern int main(int, char **);

// Second environment cell exported by libc.dylib. musl models environ as an
// alias of __environ; Mach-O cannot express aliases, so libc holds two cells
// and this stub - the single writer - stores envp into both. See environ.c.
extern char **__environ;

// libc.dylib: installs the main thread's TCB (%fs base, stack canary).
// No TLS image exists on this path - the toolchain cannot compile __thread
// for x86_64-apple-darwin - so pthread keys/TSD are the thread-local story.
extern void __cavos_setup_tls(void);

__attribute__((naked, noreturn)) void _start(void) {
  __asm__ __volatile__("movq %rsp, %rdi\n\t"
                   "andq $-16, %rsp\n\t"
                   "call _mh_start\n\t"
                   "ud2");
}

__attribute__((noreturn)) void mh_start(uint64_t stack) {
  uint64_t *values = (uint64_t *)stack;
  int       argc = (int)values[0];
  char    **argv = (char **)(values + 1);
  char    **envp = argv + argc + 1;

  environ = envp;
  __environ = envp;
  __cavos_setup_tls();
  int result = main(argc, argv);
  exit(result);
}