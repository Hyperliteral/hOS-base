// Shared scanf engine (sscanf/scanf/fscanf family).
//
// Backends supply a getc/ungetc pair over any source; the engine implements
// the full conversion set (%d %i %u %o %x %s %c %n %[...] %e %f %g %p with
// *, width and h/l/ll/hh/j/z/t/L modifiers). Numeric conversions reuse the
// strto*/strtod family on a bounded intake buffer, so overflow and endptr
// semantics match the rest of the library.
#ifndef CAVOS_INTERNAL_SCANF_IMPL_H
#define CAVOS_INTERNAL_SCANF_IMPL_H

#include <stdarg.h>
#include <stddef.h>

typedef int (*cavos_getc_fn)(void *ctx);
typedef int (*cavos_ungetc_fn)(int c, void *ctx);

int __cavos_vfscanf(void *ctx, cavos_getc_fn getc, cavos_ungetc_fn ungetc,
                    const char *format, va_list args);

#endif
