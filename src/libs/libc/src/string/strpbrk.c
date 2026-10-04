#include <string.h>

char *strpbrk(const char *s, const char *b)
{
	for (; *s; s++) {
		for (const char *p = b; *p; p++) {
			if (*s == *p)
				return (char *)s;
		}
	}
	return 0;
}
