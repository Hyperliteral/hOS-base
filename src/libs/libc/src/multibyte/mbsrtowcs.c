#include <stdlib.h>
#include <wchar.h>

// Unbounded restartable variant: len is SIZE_MAX in effect (stops at NUL).
size_t mbsrtowcs(wchar_t *restrict d, const char **restrict s, size_t len,
                 mbstate_t *restrict ps)
{
	(void)ps;
	const unsigned char *p = (const unsigned char *)*s;
	size_t i = 0;
	while (i < len && p[i]) {
		if (d)
			d[i] = p[i];
		i++;
	}
	if (p[i] == 0) {
		if (d && i < len)
			d[i] = 0;
		*s = 0;
		return i;
	}
	*s = (const char *)(p + i);
	return i;
}
