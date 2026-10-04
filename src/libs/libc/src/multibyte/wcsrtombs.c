#include <stdlib.h>
#include <wchar.h>

// Mirror of mbsrtowcs in the other direction: at most len bytes from the
// wide string (stopping at NUL), C locale 1:1 conversion. Non-ASCII wide
// input fails with EILSEQ semantics ((size_t)-1).
size_t wcsrtombs(char *restrict d, const wchar_t **restrict s, size_t len,
                 mbstate_t *restrict ps)
{
	(void)ps;
	const wchar_t *p = *s;
	size_t i = 0;
	while (i < len && *p) {
		if ((unsigned)*p > 0x7f)
			return (size_t)-1;
		if (d)
			d[i] = (char)*p;
		i++;
		p++;
	}
	if (*p == 0) {
		if (d && i < len)
			d[i] = 0;
		*s = 0;
		return i;
	}
	*s = p;
	return i;
}
