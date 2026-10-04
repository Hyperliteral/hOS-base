#include <stdlib.h>
#include <wchar.h>

// C locale: bytes convert 1:1. At most len wide chars from at most n bytes;
// a NUL inside the byte limit terminates (stored when it fits), *s goes NULL
// and the count excludes the terminator. Otherwise *s advances past the
// converted bytes. d may be NULL to measure.
size_t mbsnrtowcs(wchar_t *restrict d, const char **restrict s, size_t n,
                  size_t len, mbstate_t *restrict ps)
{
	(void)ps;
	const unsigned char *p = (const unsigned char *)*s;
	size_t i = 0;
	size_t j = 0;
	while (i < len && j < n && p[j]) {
		if (d)
			d[i] = p[j];
		i++;
		j++;
	}
	if (j < n && !p[j]) {
		if (d && i < len)
			d[i] = 0;
		*s = 0;
		return i;
	}
	*s = (const char *)(p + j);
	return i;
}
