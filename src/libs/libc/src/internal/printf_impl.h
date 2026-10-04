// Shared format engine for printf/snprintf/vsnprintf.
//
// The engine formats into a bounded buffer with no NUL added and returns the
// unbounded character count, so each frontend decides termination and return
// semantics itself (POSIX vsnprintf returns the would-be length; printf
// returns what it transmitted).
#ifndef CAVOS_INTERNAL_PRINTF_IMPL_H
#define CAVOS_INTERNAL_PRINTF_IMPL_H

#include <stdarg.h>
#include <stddef.h>

size_t __cavos_format(char *output, size_t capacity, const char *format,
                      va_list args);

#endif
