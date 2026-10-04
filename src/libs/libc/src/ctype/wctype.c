#include <ctype.h>
#include <string.h>
#include <wctype.h>

// Property descriptors by name, musl-compatible spelling ("alnum", ...).
// Only C-locale properties exist; unknown names yield 0 like musl's.
wctype_t wctype(const char *name)
{
	if (!name)
		return 0;
	if (!strcmp(name, "alnum"))
		return 1;
	if (!strcmp(name, "lower"))
		return 2;
	if (!strcmp(name, "print"))
		return 3;
	if (!strcmp(name, "upper"))
		return 4;
	return 0;
}

#undef iswctype

int iswctype(wint_t wc, wctype_t type)
{
	switch (type) {
	case 1: return iswalnum(wc);
	case 2: return iswlower(wc);
	case 3: return iswprint(wc);
	case 4: return iswupper(wc);
	default: return 0;
	}
}
