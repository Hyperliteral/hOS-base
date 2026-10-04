#include <stdlib.h>

// Classic binary search over nel width-sized elements. Returns a pointer to
// a matching element (which one is unspecified for duplicates) or NULL.
void *bsearch(const void *key, const void *base, size_t nel, size_t width,
              int (*cmp)(const void *, const void *)) {
  const unsigned char *lo = (const unsigned char *)base;
  size_t n = nel;
  while (n) {
    size_t half = n / 2;
    const unsigned char *mid = lo + half * width;
    int c = cmp(key, mid);
    if (!c)
      return (void *)mid;
    if (c < 0) {
      n = half;
    } else {
      lo = mid + width;
      n -= half + 1;
    }
  }
  return 0;
}
