// Legacy cavOS names predating the musl-compatible API. Nothing in-tree uses
// them anymore (getpid/gettid are the spellings to reach for), but out-of-tree
// Mach-O binaries may, so they stay as thin wrappers.
#include "libc.h"

long syscall_getpid(void) { return (long)getpid(); }
long syscall_gettid(void) { return (long)gettid(); }
