#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "internal/atexit_impl.h"
#include "internal/syscall.h"

// exit() runs atexit handlers LIFO, flushes the (unbuffered, so trivially
// clean) streams, then _exit()s. musl additionally runs libc-internal
// teardown; with no threads/locales/malloc-arena there is nothing to tear
// down here.
_Noreturn void exit(int status) {
  if (getenv("CAVOS_LD_DEBUG")) {
    char buf[64];
    int  n = snprintf(buf, sizeof(buf), "trace: exit(%d)\n", status);
    (void)n;
    __syscall3(SYS_write, 2, (long)buf, (long)strlen(buf));
  }
  __cavos_run_atexit();
  _exit(status);
}
