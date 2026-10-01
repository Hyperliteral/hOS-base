#ifndef SHELL_H
#define SHELL_H
#include "types.h"

// Kernel process launcher (core/boot/run.c)
bool run(char *binary, bool wait, int _argc, char **_argv);

#endif
