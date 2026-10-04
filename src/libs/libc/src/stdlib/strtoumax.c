#include <stdint.h>
#include <stdlib.h>

_Static_assert(sizeof(uintmax_t) == sizeof(unsigned long long),
              "uintmax_t width changed, revisit strtoumax");

uintmax_t strtoumax(const char *s, char **end, int base)
{
	return (uintmax_t)strtoull(s, end, base);
}
