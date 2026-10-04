#include <string.h>

// strlcat: append src to a NUL-terminated dst of total buffer size, always
// NUL-terminate when size > dstlen + 1, return dstlen + srclen so
// truncation is detectable (ret >= size). dstlen is capped at size: an
// unterminated dst counts as length size.
size_t strlcat(char *dst, const char *src, size_t size) {
  size_t dstlen = strnlen(dst, size);
  size_t srclen = strlen(src);
  if (dstlen < size) {
    size_t room = size - dstlen - 1;
    size_t copy = srclen < room ? srclen : room;
    memcpy(dst + dstlen, src, copy);
    dst[dstlen + copy] = '\0';
  }
  return dstlen + srclen;
}
