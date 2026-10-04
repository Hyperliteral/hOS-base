// atof/atol/atoll as thin wrappers over the real converters.
#include <stdlib.h>

double atof(const char *s) { return strtod(s, 0); }
