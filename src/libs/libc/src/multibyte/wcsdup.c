#include <stdlib.h>
#include <string.h>
#include <wchar.h>

wchar_t *wcsdup(const wchar_t *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	wchar_t *d = malloc((n + 1) * sizeof(*d));
	if (!d)
		return 0;
	for (size_t i = 0; i <= n; i++)
		d[i] = s[i];
	return d;
}
