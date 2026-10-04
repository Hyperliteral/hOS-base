#include <ctype.h>
#include <wctype.h>

#undef iswupper

int iswupper(wint_t wc)
{
	if (wc > 0x7f)
		return 0;
	return isupper((int)wc);
}
