// strsep: unlike strtok_r, every delimiter ends a token, so empty fields
// ("a,,b") yield empty strings. The input pointer advances past the token.
#include <string.h>

char *strsep(char **stringp, const char *delim) {
  if (!stringp || !*stringp)
    return 0;
  char *s = *stringp;
  size_t len = strcspn(s, delim);
  if (s[len]) {
    s[len] = '\0';
    *stringp = s + len + 1;
  } else {
    *stringp = 0;
  }
  return s;
}
