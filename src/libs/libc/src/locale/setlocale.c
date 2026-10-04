#include <locale.h>
#include <string.h>

// Only "C" (and its alias "POSIX") exist here: no locale files, no multibyte
// tables, no message catalogs. Anything else fails with NULL, which is also
// what musl returns for missing locales - callers fall back to C.
char *setlocale(int category, const char *locale)
{
	(void)category;
	if (!locale)
		return (char *)"C";
	if (!strcmp(locale, "C") || !strcmp(locale, "POSIX") ||
	    !strcmp(locale, ""))
		return (char *)"C";
	return 0;
}
