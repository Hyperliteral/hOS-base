#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

// Parsed as double, then narrowed: values outside float range clamp to
// HUGE_VALF + ERANGE. Decimal strings that need more than float precision
// can double-round in the last ulp versus direct parsing - documented, and
// irrelevant to every in-tree consumer.
float strtof(const char *s, char **end)
{
	double v = strtod(s, end);
	if (v > (double)FLT_MAX || v < -(double)FLT_MAX) {
		*__errno_location() = ERANGE;
		return v < 0 ? -HUGE_VALF : HUGE_VALF;
	}
	return (float)v;
}
