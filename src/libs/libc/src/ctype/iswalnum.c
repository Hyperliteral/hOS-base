#include <ctype.h>
#include <wctype.h>

// Wide classification in the C locale: ASCII characters classify exactly
// like their narrow twins, everything else is "not a member". tow* are the
// identity outside ASCII. Full Unicode tables (musl's locale data) do not
// exist here - setlocale() only ever provides "C" anyway.
#undef iswalnum

int iswalnum(wint_t wc)
{
	if (wc > 0x7f)
		return 0;
	return isalnum((int)wc);
}
