#include <stdio.h>
#include <wchar.h>

wint_t btowc(int c)
{
	if (c == EOF)
		return WEOF;
	return (wint_t)(wchar_t)(unsigned char)c;
}
