// cavOS libc compatibility shim.
//
// New code should include the standard headers directly (<stdio.h>,
// <stdlib.h>, <string.h>, <unistd.h>, ...); they are vendored from musl and
// declare the same API with the same signatures, so anything that compiles
// against musl compiles against this libc. This header exists so that existing
// sources doing `#include <libc.h>` - the Mach-O entry stub and the
// macho-test program - keep building unchanged.
#ifndef CAVOS_LIBC_H
#define CAVOS_LIBC_H

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Legacy cavOS spellings predating the musl-compatible API. Prefer
// getpid()/gettid(); these remain for out-of-tree binaries built against the
// original single-file libc.
long syscall_getpid(void);
long syscall_gettid(void);

#ifdef __cplusplus
}
#endif

#endif
