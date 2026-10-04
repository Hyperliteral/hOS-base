#include <math.h>

float nanf(const char *s)
{
	(void)s; // posix
	return NAN;
}
