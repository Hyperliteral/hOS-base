#include <wchar.h>

// C locale: NUL is 0, ASCII printable (space included) is 1, everything else
// is non-printable (-1). Matches musl's C-locale table.
int wcwidth(wchar_t wc)
{
	if (!wc)
		return 0;
	if ((unsigned)wc < 0x20 || (unsigned)wc >= 0x7f)
		return -1;
	return 1;
}
