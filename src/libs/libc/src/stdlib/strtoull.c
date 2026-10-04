#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

// Same engine as strtoul with unsigned long long limits.
unsigned long long strtoull(const char *s, char **end, int base)
{
	const char *p = s;
	while (isspace((unsigned char)*p))
		p++;
	int neg = 0;
	if (*p == '-' || *p == '+')
		neg = *p++ == '-';
	if ((base == 0 || base == 16) && p[0] == '0' &&
	    (p[1] | 32) == 'x' && isxdigit((unsigned char)p[2])) {
		p += 2;
		base = 16;
	} else if (base == 0) {
		base = p[0] == '0' ? 8 : 10;
	}
	if (base < 2 || base > 36) {
		*__errno_location() = EINVAL;
		if (end)
			*end = (char *)s;
		return 0;
	}
	unsigned long long cutoff = ULLONG_MAX;
	unsigned cutlim = (unsigned)(cutoff % (unsigned long long)base);
	cutoff /= (unsigned long long)base;
	unsigned long long n = 0;
	int overflow = 0;
	int any = 0;
	for (;; p++) {
		int c = (unsigned char)*p;
		int d = -1;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if ((c | 32) >= 'a' && (c | 32) <= 'z')
			d = (c | 32) - 'a' + 10;
		if (d < 0 || d >= base)
			break;
		any = 1;
		if (n > cutoff || (n == cutoff && (unsigned)d > cutlim))
			overflow = 1;
		else
			n = n * (unsigned long long)base + (unsigned long long)d;
	}
	if (!any) {
		if (end)
			*end = (char *)s;
		return 0;
	}
	if (overflow)
		*__errno_location() = ERANGE;
	if (end)
		*end = (char *)p;
	if (overflow)
		return ULLONG_MAX;
	return neg ? -n : n;
}
