#include <stdlib.h>
#include <wchar.h>

int wctomb(char *s, wchar_t wc)
{
	if (!s)
		return 0;
	if ((unsigned)wc > 0x7f)
		return -1;
	*s = (char)wc;
	return 1;
}
