#include <ctype.h>
#include <wctype.h>

#undef iswlower

int iswlower(wint_t wc)
{
	if (wc > 0x7f)
		return 0;
	return islower((int)wc);
}
