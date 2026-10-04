#include <stdint.h>
#include <stdlib.h>

// intmax_t is 64 bits here, exactly the long long range: direct delegation
// with a compile-time pin so a width change breaks loudly instead of
// silently narrowing.
_Static_assert(sizeof(intmax_t) == sizeof(long long),
              "intmax_t width changed, revisit strtoimax");

intmax_t strtoimax(const char *s, char **end, int base)
{
	return (intmax_t)strtoll(s, end, base);
}
