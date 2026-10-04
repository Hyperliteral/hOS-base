#include <ctype.h>
#include <strings.h>

int strncasecmp(const char *l, const char *r, size_t n)
{
	for (; n && tolower((unsigned char)*l) == tolower((unsigned char)*r);
	     n--, l++, r++) {
		if (!*l)
			break;
	}
	if (!n)
		return 0;
	return tolower((unsigned char)*l) - tolower((unsigned char)*r);
}
