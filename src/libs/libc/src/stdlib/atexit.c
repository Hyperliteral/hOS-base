#include <stdlib.h>

#include "internal/atexit_impl.h"

#define ATEXIT_SLOTS 32

static struct {
  cavos_exit_fn fn;
  void         *arg;
  void         *dso;
} atexit_list[ATEXIT_SLOTS];
static size_t atexit_used;

static int register_exit(cavos_exit_fn fn, void *arg, void *dso) {
  if (!fn || atexit_used >= ATEXIT_SLOTS)
    return -1;
  atexit_list[atexit_used].fn = fn;
  atexit_list[atexit_used].arg = arg;
  atexit_list[atexit_used].dso = dso;
  atexit_used++;
  return 0;
}

int atexit(void (*fn)(void)) {
  return register_exit((cavos_exit_fn)fn, 0, 0);
}

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso) {
  return register_exit((cavos_exit_fn)fn, arg, dso);
}

void __cxa_finalize(void *dso) {
  while (atexit_used > 0) {
    size_t last = atexit_used - 1;
    if (dso && atexit_list[last].dso != dso)
      break; // only unwind this dso's own tail entries
    atexit_used = last;
    atexit_list[last].fn(atexit_list[last].arg);
  }
}

void __cavos_run_atexit(void) { __cxa_finalize(0); }
