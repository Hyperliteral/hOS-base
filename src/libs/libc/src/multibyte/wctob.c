#include <stdio.h>
#include <wchar.h>

int wctob(wint_t wc)
{
	if ((unsigned)wc > 0x7f)
		return EOF;
	return (int)(unsigned char)wc;
}
