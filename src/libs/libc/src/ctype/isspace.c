#include <ctype.h>

// Function bodies match musl-1.2.5 (src/ctype/isspace.c); the locale and
// weak-alias tails are omitted - this libc has neither locales nor Mach-O
// weak-alias support.
#undef isspace

int isspace(int c)
{
	return c == ' ' || (unsigned)c-'\t' < 5;
}
