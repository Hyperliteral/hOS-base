#include <unistd.h>

#include "internal/syscall.h"

int execve(const char *path, char *const argv[], char *const envp[]) {
  return (int)__cavos_ret((unsigned long)__syscall3(SYS_execve, (long)path,
                                                   (long)argv, (long)envp));
}
