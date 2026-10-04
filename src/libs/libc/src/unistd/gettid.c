#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

// _GNU_SOURCE in musl's unistd.h; provided unconditionally here since it is
// one syscall and costs nothing.
pid_t gettid(void) {
  return (pid_t)__cavos_ret((unsigned long)__syscall0(SYS_gettid));
}
