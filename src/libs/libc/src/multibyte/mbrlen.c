#include <stdlib.h>
#include <wchar.h>

size_t mbrlen(const char *restrict s, size_t n, mbstate_t *restrict ps)
{
	(void)ps;
	if (!s)
		return 0;
	if (!n)
		return (size_t)-2;
	return *s ? 1 : 0;
}
