// Declarations for musl-private (double-underscore) helpers this libc
// provides itself.
//
// musl's own build declares these through src/internal/*.h, which we do not
// vendor (that tree drags in the thread implementation). The few vendored
// sources that reference them include this header instead; see README.md.
// Mach-O has no weak-alias support, so the public names (stpcpy, memrchr,
// ...) are plain wrappers defined next to their double-underscore originals
// rather than weak aliases.
#ifndef CAVOS_INTERNAL_IMPL_H
#define CAVOS_INTERNAL_IMPL_H

#include <stddef.h>

extern char **__environ;

char *__strchrnul(const char *s, int c);
void *__memrchr(const void *m, int c, size_t n);
char *__stpcpy(char *restrict d, const char *restrict s);
char *__stpncpy(char *restrict d, const char *restrict s, size_t n);

#endif
