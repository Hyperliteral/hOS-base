#include <wchar.h>

// C locale collation is code-point order: identical to wcscmp (see
// strcoll.c for the narrow twin).
int wcscoll(const wchar_t *l, const wchar_t *r) { return wcscmp(l, r); }
