#include <stdint.h>

// Stack-smashing canary cell. musl keeps this in TLS; this libc has no
// threads, so one global cell serves every thread that will ever exist.
// It is reseeded from AT_RANDOM on every exec - by the program interpreter
// (src/ldso) on the dynamic path and by __libc_start_main on the static
// path - and starts as a fixed nonzero constant so binaries that bypass
// both (none in-tree) still check *something*.
uintptr_t __stack_chk_guard = 0x595e5a5b5c5d5e5fULL;
