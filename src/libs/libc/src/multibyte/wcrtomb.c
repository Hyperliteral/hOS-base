#include <stdlib.h>
#include <wchar.h>

size_t wcrtomb(char *restrict s, wchar_t wc, mbstate_t *restrict ps)
{
	(void)ps;
	if (!s)
		return 1;
	if ((unsigned)wc > 0x7f)
		return (size_t)-1;
	*s = (char)wc;
	return 1;
}
