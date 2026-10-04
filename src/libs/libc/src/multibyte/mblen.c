#include <stdlib.h>
#include <wchar.h>

// C locale is stateless and single-byte: every byte is a complete character
// identical to its wide value. ps is accepted and ignored.
int mblen(const char *s, size_t n)
{
	if (!s)
		return 0;
	return n && *s ? 1 : (n ? 0 : -1);
}
