#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

// Same double-rounding note as strtof: parsed as double, widened. The extra
// long double precision past double is not recovered.
long double strtold(const char *s, char **end)
{
	double v = strtod(s, end);
	if (v > (double)LDBL_MAX || v < -(double)LDBL_MAX) {
		*__errno_location() = ERANGE;
		return v < 0 ? -HUGE_VALL : HUGE_VALL;
	}
	return (long double)v;
}
