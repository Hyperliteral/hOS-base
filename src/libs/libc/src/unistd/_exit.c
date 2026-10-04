#include <unistd.h>

#include "internal/syscall.h"

_Noreturn void _exit(int status) {
  __syscall1(SYS_exit_group, status);
  __builtin_unreachable();
}
