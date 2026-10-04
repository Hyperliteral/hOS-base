#include <ctype.h>
#include <string.h>

char *strcasestr(const char *h, const char *n)
{
	size_t nl = strlen(n);
	if (!*n)
		return (char *)h;
	for (; *h; h++) {
		if (tolower((unsigned char)*h) != tolower((unsigned char)*n))
			continue;
		size_t i = 0;
		for (; i < nl; i++) {
			if (tolower((unsigned char)h[i]) !=
			    tolower((unsigned char)n[i]))
				break;
		}
		if (i == nl)
			return (char *)h;
	}
	return 0;
}
