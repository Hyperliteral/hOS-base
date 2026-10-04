#include <ctype.h>
#include <wctype.h>

#undef iswprint

int iswprint(wint_t wc)
{
	if (wc > 0x7f)
		return 0;
	return isprint((int)wc);
}
