#include <string.h>

// Vendored from musl-1.2.5 (src/string/strrchr.c), adapted as in strchr.c.
#include "internal/impl.h"

char *strrchr(const char *s, int c)
{
	return __memrchr(s, c, strlen(s) + 1);
}
