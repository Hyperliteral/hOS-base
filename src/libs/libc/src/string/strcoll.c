#include <string.h>

// C locale collation is byte order: identical to strcmp. (No other locale
// exists here, so this is exact rather than approximate.)
int strcoll(const char *l, const char *r) { return strcmp(l, r); }
