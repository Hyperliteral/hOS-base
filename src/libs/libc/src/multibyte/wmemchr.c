#include <wchar.h>

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
	for (; n; n--, s++) {
		if (*s == c)
			return (wchar_t *)s;
	}
	return 0;
}
