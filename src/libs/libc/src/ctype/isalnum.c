#include <ctype.h>

// Bodies match musl-1.2.5's range checks; locale/alias tails omitted (see
// isspace.c). Only the C locale exists here, so ASCII tables are exact.
#undef isalnum

int isalnum(int c)
{
	return (unsigned)c-'0' < 10 || ((unsigned)(c|32)-'a') < 26;
}
