#include <string.h>

// Vendored from musl-1.2.5 (src/string/strchr.c). The internal header stands
// in for musl's private declarations - see src/internal/impl.h.
#include "internal/impl.h"

char *strchr(const char *s, int c)
{
	char *r = __strchrnul(s, c);
	return *(unsigned char *)r == (unsigned char)c ? r : 0;
}
