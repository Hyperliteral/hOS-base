#include <ctype.h>
#include <wctype.h>

#undef towupper

wint_t towupper(wint_t wc)
{
	if (wc > 0x7f)
		return wc;
	return (wint_t)toupper((int)wc);
}
