#include <math.h>

double nan(const char *s)
{
	(void)s; // posix needs s. dont remove even if unused
	return NAN;
}
