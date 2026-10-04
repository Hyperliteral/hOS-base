#include <string.h>
#include <strings.h>

// Legacy BSD spelling: argument order is (src, dst), unlike memmove.
void bcopy(const void *src, void *dst, size_t n) { memmove(dst, src, n); }
