// atexit/__cxa_atexit/__cxa_finalize machinery.
//
// A small LIFO list of (function, argument, dso) triples. Plain atexit()
// registers with a NULL argument; __cxa_atexit carries one. __cxa_finalize
// with a NULL dso runs everything (what exit() does); with a dso it would
// drop that library's entries (only meaningful with dlclose unloading,
// which never unloads - see src/ldso/dlclose.c).
#ifndef CAVOS_INTERNAL_ATEXIT_IMPL_H
#define CAVOS_INTERNAL_ATEXIT_IMPL_H

typedef void (*cavos_exit_fn)(void *);

void __cavos_run_atexit(void);

#endif
