#include <errno.h>
#include <stdlib.h>
#include <wchar.h>

size_t mbrtowc(wchar_t *restrict wc, const char *restrict s, size_t n,
               mbstate_t *restrict ps)
{
	(void)ps;
	if (!s)
		return 0;
	if (!n)
		return (size_t)-2;
	if (!*s) {
		if (wc)
			*wc = 0;
		return 0;
	}
	if (wc)
		*wc = (unsigned char)*s;
	return 1;
}
