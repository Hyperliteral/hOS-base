// dirname (libgen.h) lives here per libc layout: one file per string
// function. Follows musl semantics: trailing slashes stripped (root stays
// "/"), the last component cut, no slash at all yields ".". May modify the
// input; the "." and "/" results are static.
#include <libgen.h>
#include <stddef.h>
#include <string.h>

char *dirname(char *s) {
  static char dot[] = ".";
  static char slash[] = "/";
  if (!s || !*s)
    return dot;
  size_t len = strlen(s);
  while (len > 1 && s[len - 1] == '/') {
    s[--len] = '\0';
  }
  char *last = strrchr(s, '/');
  if (!last)
    return dot;
  if (last == s)
    return slash;
  *last = '\0';
  if (!*s)
    return dot;
  return s;
}
