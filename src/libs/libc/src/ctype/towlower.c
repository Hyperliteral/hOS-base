#include <ctype.h>
#include <wctype.h>

#undef towlower

wint_t towlower(wint_t wc)
{
	if (wc > 0x7f)
		return wc;
	return (wint_t)tolower((int)wc);
}
