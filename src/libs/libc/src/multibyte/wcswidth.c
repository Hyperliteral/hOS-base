#include <wchar.h>

int wcswidth(const wchar_t *s, size_t n)
{
	int width = 0;
	for (size_t i = 0; i < n && s[i]; i++) {
		int w = wcwidth(s[i]);
		if (w < 0)
			return -1;
		width += w;
	}
	return width;
}
