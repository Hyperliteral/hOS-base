#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "internal/pthread_impl.h"
#include "internal/syscall.h"

// fork() runs atfork handlers around a raw SYS_fork: prepare (which parks
// the heap lock) before, parent/child after. musl additionally locks its
// thread list; with no thread census the heap rebalance is what matters.
pid_t fork(void) {
  __cavos_atfork_prepare();
  pid_t r = (pid_t)__cavos_ret((unsigned long)__syscall0(SYS_fork));
  if (r == 0)
    __cavos_atfork_child();
  else
    __cavos_atfork_parent();
  return r;
}
