#include <string.h>

// C locale transformation is the identity: copy (bounded) and report the
// full source length, exactly like musl does when LC_COLLATE=C.
size_t strxfrm(char *restrict d, const char *restrict s, size_t n)
{
	size_t len = strlen(s);
	if (n) {
		size_t copy = len < n ? len : n - 1;
		memcpy(d, s, copy);
		d[copy] = 0;
	}
	return len;
}
