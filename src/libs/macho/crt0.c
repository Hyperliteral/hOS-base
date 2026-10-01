#include <stdint.h>
#include "macho.h"

extern int main(int, char **);

__attribute__((naked, noreturn)) void _start(void) {
  __asm__ volatile("movq %rsp, %rdi\n\t"
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
  int result = main(argc, argv);
  exit(result);
}
