#include <string.h>

void *__memrchr(const void *m, int c, size_t n)
{
	const unsigned char *s = m;
	c = (unsigned char)c;
	while (n--) if (s[n]==c) return (void *)(s+n);
	return 0;
}

/* See strchrnul.c for why this is a plain wrapper, not a weak alias. */
void *memrchr(const void *m, int c, size_t n)
{
	return __memrchr(m, c, n);
}
