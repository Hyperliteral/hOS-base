#include <wchar.h>

int wcsncmp(const wchar_t *l, const wchar_t *r, size_t n)
{
	for (; n && *l == *r; n--, l++, r++) {
		if (!*l)
			break;
	}
	if (!n)
		return 0;
	return *l < *r ? -1 : *l != *r;
}
