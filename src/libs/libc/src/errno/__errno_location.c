#include <errno.h>

#include "internal/pthread_impl.h"

// Thread-local errno hung off the TCB. Before the thread pointer is
// installed (ldso self-relocation, pre-TLS crashes) TP reads 0 and errno
// falls back to a single global cell - failures there still report, they
// just are not thread-isolated yet.
static int fallback_errno;

int *__errno_location(void) {
  struct __pthread *t = __cavos_self();
  if (!t || t->self != t)
    return &fallback_errno;
  return &t->errno_val;
}

// Three-underscore spelling some musl-built objects reference. Same cell,
// plain definition: Mach-O cannot express the weak alias musl uses.
int *___errno_location(void) { return __errno_location(); }
