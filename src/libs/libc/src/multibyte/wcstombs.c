#include <stdlib.h>
#include <wchar.h>

size_t wcstombs(char *restrict s, const wchar_t *restrict w, size_t n)
{
	size_t i = 0;
	for (; i < n && w[i]; i++) {
		if ((unsigned)w[i] > 0x7f)
			return (size_t)-1;
		s[i] = (char)w[i];
	}
	if (i < n)
		s[i] = 0;
	return i;
}
