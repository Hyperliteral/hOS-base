#include <ctype.h>
#include <strings.h>

int strcasecmp(const char *l, const char *r)
{
	for (; tolower((unsigned char)*l) == tolower((unsigned char)*r) && *l;
	     l++, r++);
	return tolower((unsigned char)*l) - tolower((unsigned char)*r);
}
