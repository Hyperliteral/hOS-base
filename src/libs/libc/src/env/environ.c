// Process environment pointer(s).
//
// musl models this as one cell, __environ, with environ as a weak alias.
// Mach-O has no weak aliases, so this libc holds two cells instead, and the
// single writer - the program entry stub (src/libs/macho/crt0.c) - stores
// envp into both before main runs. Readers must therefore observe the same
// pointer through either name; grep must show no other writer.
#include <unistd.h>

char **environ = 0;
char **__environ = 0;
