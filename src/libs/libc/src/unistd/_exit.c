#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "internal/syscall.h"

_Noreturn void _exit(int status) {
  if (getenv("CAVOS_LD_DEBUG")) {
    char buf[64];
    int  n = snprintf(buf, sizeof(buf), "trace: _exit(%d)\n", status);
    (void)n;
    __syscall3(SYS_write, 2, (long)buf, (long)strlen(buf));
  }
  __syscall1(SYS_exit_group, status);
  __builtin_unreachable();
}
