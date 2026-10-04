#include <stdlib.h>
#include <wchar.h>

int mbtowc(wchar_t *restrict wc, const char *restrict s, size_t n)
{
	if (!s)
		return 0;
	if (!n || !*s)
		return n ? 0 : -1;
	if (wc)
		*wc = (unsigned char)*s;
	return 1;
}
