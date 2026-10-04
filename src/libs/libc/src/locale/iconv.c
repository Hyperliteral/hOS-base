#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

// Minimal iconv for the C locale world: UTF-8 <-> UTF-8 (validated copy)
// and the ASCII family (validated <0x80). Anything else fails at open with
// EINVAL. This covers every conversion bash/readline/ncurses attempt under
// the C locale; full transcoding tables are a later milestone, tracked
// openly like the regex engine.
typedef struct CavosIconv {
  int ascii_only; // reject anything above 0x7f when set
} CavosIconv;

static void upper_nospace(const char *s, char *out, size_t n) {
  size_t i = 0;
  while (*s && i + 1 < n) {
    char c = *s++;
    if (c == '-' || c == '_' || c == ' ')
      continue;
    out[i++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
  }
  out[i] = 0;
}

static int is_utf8(const char *name) {
  char norm[32];
  upper_nospace(name, norm, sizeof(norm));
  return !strcmp(norm, "UTF8");
}

static int is_ascii(const char *name) {
  char norm[32];
  upper_nospace(name, norm, sizeof(norm));
  return !strcmp(norm, "ASCII") || !strcmp(norm, "646") ||
         !strcmp(norm, "646US") || !strcmp(norm, "USASCII") ||
         !strcmp(norm, "646US2") || !strcmp(norm, "646IRV");
}

iconv_t iconv_open(const char *tocode, const char *fromcode) {
  int to_utf8 = is_utf8(tocode);
  int from_utf8 = is_utf8(fromcode);
  int to_ascii = is_ascii(tocode);
  int from_ascii = is_ascii(fromcode);
  if ((!to_utf8 && !to_ascii) || (!from_utf8 && !from_ascii)) {
    *__errno_location() = EINVAL; // EINVAL: unsupported conversion
    return (iconv_t)-1;
  }
  CavosIconv *cd = malloc(sizeof(*cd));
  if (!cd)
    return (iconv_t)-1;
  cd->ascii_only = to_ascii || from_ascii;
  return (iconv_t)cd;
}

// Returns bytes consumed (>0), -1 for EILSEQ, -2 for EINVAL (truncated).
static int utf8_decode(const unsigned char **pp, const unsigned char *end,
                       unsigned *cp) {
  unsigned char c = **pp;
  if (c < 0x80) {
    *cp = c;
    (*pp)++;
    return 1;
  }
  int      len;
  unsigned min;
  if ((c & 0xe0) == 0xc0) {
    len = 2;
    min = 0x80;
    *cp = (unsigned)(c & 0x1f);
  } else if ((c & 0xf0) == 0xe0) {
    len = 3;
    min = 0x800;
    *cp = (unsigned)(c & 0x0f);
  } else if ((c & 0xf8) == 0xf0) {
    len = 4;
    min = 0x10000;
    *cp = (unsigned)(c & 0x07);
  } else {
    return -1;
  }
  if (*pp + len > end)
    return -2;
  for (int i = 1; i < len; i++) {
    unsigned char d = (*pp)[i];
    if ((d & 0xc0) != 0x80)
      return -1;
    *cp = (*cp << 6) | (d & 0x3f);
  }
  if (*cp < min || (*cp >= 0xd800 && *cp <= 0xdfff) || *cp > 0x10ffff)
    return -1;
  *pp += len;
  return len;
}

static int utf8_encode(unsigned cp, unsigned char *out) {
  if (cp < 0x80) {
    out[0] = (unsigned char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (unsigned char)(0xc0 | (cp >> 6));
    out[1] = (unsigned char)(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (unsigned char)(0xe0 | (cp >> 12));
    out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
    out[2] = (unsigned char)(0x80 | (cp & 0x3f));
    return 3;
  }
  out[0] = (unsigned char)(0xf0 | (cp >> 18));
  out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
  out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
  out[3] = (unsigned char)(0x80 | (cp & 0x3f));
  return 4;
}

size_t iconv(iconv_t cd, char **restrict inbytes, size_t *restrict inleft,
             char **restrict outbytes, size_t *restrict outleft) {
  CavosIconv *conv = (CavosIconv *)cd;
  if (!inbytes || !*inbytes)
    return 0; // reset: stateless, nothing to do
  const unsigned char *in = (const unsigned char *)*inbytes;
  const unsigned char *in_end = in + *inleft;
  unsigned char       *out = (unsigned char *)*outbytes;
  size_t               left = *outleft;
  while (in < in_end) {
    const unsigned char *start = in;
    unsigned             cp;
    int r = utf8_decode(&in, in_end, &cp);
    if (r < 0) {
      *__errno_location() = r == -2 ? EINVAL : EILSEQ; // EINVAL or EILSEQ
      *inbytes = (char *)start;
      *inleft = (size_t)(in_end - start);
      *outbytes = (char *)out;
      *outleft = left;
      return (size_t)-1;
    }
    if (conv->ascii_only && cp > 0x7f) {
      *__errno_location() = EILSEQ; // EILSEQ
      *inbytes = (char *)start;
      *inleft = (size_t)(in_end - start);
      *outbytes = (char *)out;
      *outleft = left;
      return (size_t)-1;
    }
    unsigned char tmp[4];
    int           n = utf8_encode(cp, tmp);
    if ((size_t)n > left) {
      *__errno_location() = E2BIG; // E2BIG
      *inbytes = (char *)start;
      *inleft = (size_t)(in_end - start);
      *outbytes = (char *)out;
      *outleft = left;
      return (size_t)-1;
    }
    for (int i = 0; i < n; i++) {
      *out++ = tmp[i];
      left--;
    }
    *inbytes = (char *)in;
    *inleft = (size_t)(in_end - in);
    *outbytes = (char *)out;
    *outleft = left;
  }
  return 0;
}

int iconv_close(iconv_t cd) {
  free(cd);
  return 0;
}
