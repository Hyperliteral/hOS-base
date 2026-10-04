// strtok_r: reentrant token splitter; consecutive delimiters collapse and
// leading delimiters are skipped. *lasts holds the resume pointer.
#include <string.h>

char *strtok_r(char *restrict s, const char *restrict delim,
               char **restrict lasts) {
  if (!s)
    s = *lasts;
  s += strspn(s, delim);
  if (!*s) {
    *lasts = s;
    return 0;
  }
  size_t len = strcspn(s, delim);
  if (s[len]) {
    s[len] = '\0';
    *lasts = s + len + 1;
  } else {
    *lasts = s + len;
  }
  return s;
}
