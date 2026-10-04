#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Decimal (and C99 hexadecimal) string to double. Mantissa accumulates up to
// 19 significant digits exactly with the exponent tracking the rest, then
// scales in long double - correctly rounded except for adversarial digit
// strings past the 19th digit. Overflow yields HUGE_VAL + ERANGE; underflow
// returns signed zero (no subnormal ERANGE quibbles - documented).
static int hexdig(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if ((c | 32) >= 'a' && (c | 32) <= 'f')
		return (c | 32) - 'a' + 10;
	return -1;
}

double strtod(const char *s, char **end)
{
	const char *p = s;
	while (isspace((unsigned char)*p))
		p++;
	int neg = 0;
	if (*p == '-' || *p == '+')
		neg = *p++ == '-';
	double sign = neg ? -1.0 : 1.0;

	if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f') {
		p += 3;
		if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' &&
		    (p[2] | 32) == 'i' && (p[3] | 32) == 't' &&
		    (p[4] | 32) == 'y')
			p += 5;
		if (end)
			*end = (char *)p;
		return sign * INFINITY;
	}
	if ((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n') {
		p += 3;
		if (*p == '(') {
			p++;
			while (*p && *p != ')')
				p++;
			if (*p == ')')
				p++;
		}
		if (end)
			*end = (char *)p;
		return sign * NAN;
	}

	int hex = 0;
	if (p[0] == '0' && (p[1] | 32) == 'x' && hexdig(p[2]) >= 0) {
		hex = 1;
		p += 2;
	}
	unsigned base = hex ? 16 : 10;
	unsigned long long acc = 0;
	int acc_digits = 0;
	long mexp = 0; // value = acc * base^mexp, times 2^eexp below
	long eexp = 0;
	int any = 0;
	int d;
	while ((d = hex ? hexdig(*p)
	                : (isdigit((unsigned char)*p) ? *p - '0' : -1)) >= 0) {
		if (acc_digits < 19) {
			acc = acc * base + (unsigned)d;
			acc_digits++;
		} else {
			mexp++;
		}
		any = 1;
		p++;
	}
	if (*p == '.') {
		p++;
		while ((d = hex ? hexdig(*p)
		                : (isdigit((unsigned char)*p) ? *p - '0' : -1)) >= 0) {
			if (acc_digits < 19) {
				acc = acc * base + (unsigned)d;
				acc_digits++;
				mexp--;
			}
			any = 1;
			p++;
		}
	}
	if ((hex && (*p == 'p' || *p == 'P')) ||
	    (!hex && (*p == 'e' || *p == 'E'))) {
		const char *e = p + 1;
		int esign = 0;
		if (*e == '-' || *e == '+')
			esign = *e++ == '-';
		long ev = 0;
		int edigits = 0;
		while (isdigit((unsigned char)*e)) {
			if (ev < 1000000)
				ev = ev * 10 + (*e - '0');
			edigits++;
			e++;
		}
		if (edigits) {
			p = e;
			if (hex)
				eexp += esign ? -ev : ev;
			else
				mexp += esign ? -ev : ev;
		}
	}
	if (!any) {
		if (end)
			*end = (char *)s;
		return 0.0;
	}
	if (end)
		*end = (char *)p;

	// Scale: mantissa base first, then the binary exponent (hex only).
	long double v = (long double)acc;
	if (hex) {
		while (mexp > 0) {
			v *= 16;
			mexp--;
			if (v > LDBL_MAX)
				goto overflow;
		}
		while (mexp < 0) {
			v /= 16;
			mexp++;
		}
		while (eexp > 0) {
			v *= 2;
			eexp--;
			if (v > LDBL_MAX)
				goto overflow;
		}
		while (eexp < 0) {
			v /= 2;
			eexp++;
		}
	} else {
		while (mexp > 0) {
			v *= 10;
			mexp--;
			if (v > LDBL_MAX)
				goto overflow;
		}
		while (mexp < 0) {
			v /= 10;
			mexp++;
		}
	}
	if (v > (long double)DBL_MAX)
		goto overflow;
	return (double)(neg ? -v : v);

overflow:
	*__errno_location() = ERANGE;
	return sign * HUGE_VAL;
}
