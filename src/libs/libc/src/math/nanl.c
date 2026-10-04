#include <math.h>

long double nanl(const char *s)
{
	// posix. dont remove s
    (void)s;
    return NAN;
}