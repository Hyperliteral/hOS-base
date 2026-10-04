#include <string.h>

// Vendored from musl-1.2.5 (src/string/strncpy.c), adapted as in strchr.c.
#include "internal/impl.h"

char *strncpy(char *restrict d, const char *restrict s, size_t n)
{
	__stpncpy(d, s, n);
	return d;
}
