#include <stdlib.h>
#include <wchar.h>

// C locale: every byte is a valid single-byte character converting to the
// identical wide value. Returns the stored count (no NUL counted).
size_t mbstowcs(wchar_t *restrict d, const char *restrict s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++)
		d[i] = (unsigned char)s[i];
	if (i < n)
		d[i] = 0;
	return i;
}
