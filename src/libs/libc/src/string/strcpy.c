#include <string.h>

// Vendored from musl-1.2.5 (src/string/strcpy.c), adapted as in strchr.c.
#include "internal/impl.h"

char *strcpy(char *restrict dest, const char *restrict src)
{
	__stpcpy(dest, src);
	return dest;
}
