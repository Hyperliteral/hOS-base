#include <string.h>

// memmem: first occurrence of the needle in the haystack. An empty needle
// matches at the start; memcmp-backed scan, no tables.
void *memmem(const void *hay, size_t hay_len, const void *needle,
             size_t needle_len) {
  const unsigned char *h = hay;
  const unsigned char *n = needle;
  if (!hay_len || !needle_len)
    return (void *)h;
  if (needle_len > hay_len)
    return 0;
  unsigned char first = n[0];
  size_t last = hay_len - needle_len;
  for (size_t i = 0; i <= last; i++) {
    if (h[i] == first && memcmp(h + i, n, needle_len) == 0)
      return (void *)(h + i);
  }
  return 0;
}
