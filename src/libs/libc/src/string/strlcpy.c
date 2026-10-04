#include <string.h>

// strlcpy: copy up to size-1 bytes, always NUL-terminate when size > 0,
// return the full source length so truncation is detectable (ret >= size).
size_t strlcpy(char *dst, const char *src, size_t size) {
  size_t srclen = strlen(src);
  if (size > 0) {
    size_t copy = srclen < size - 1 ? srclen : size - 1;
    memcpy(dst, src, copy);
    dst[copy] = '\0';
  }
  return srclen;
}
