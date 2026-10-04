#include <ctype.h>

// Function body matches musl-1.2.5 (src/ctype/isdigit.c); see isspace.c for
// why the locale/alias tail is omitted.
#undef isdigit

int isdigit(int c)
{
	return (unsigned)c-'0' < 10;
}
