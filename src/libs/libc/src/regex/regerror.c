#include <regex.h>
#include <string.h>

size_t regerror(int errcode, const regex_t *restrict preg, char *restrict buf,
                size_t len) {
  const char *message;
  (void)preg;
  switch (errcode) {
  case REG_BADPAT: message = "invalid pattern (regex engine not implemented)"; break;
  case REG_NOMATCH: message = "no match"; break;
  default: message = "unknown regex error"; break;
  }
  size_t full = strlen(message);
  if (len) {
    size_t copy = full < len ? full : len - 1;
    memcpy(buf, message, copy);
    buf[copy] = 0;
  }
  return full + 1;
}
