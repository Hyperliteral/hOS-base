#include <regex.h>
#include <stdlib.h>
#include <string.h>

// POSIX regex is a full engine (musl ships TRE for this); vendoring it is a
// later milestone, tracked openly. Until then these stubs link cleanly and
// fail loudly at runtime: regcomp returns REG_BADPAT, regexec reports
// REG_NOMATCH... no - it must not pretend to match. Contract: regcomp
// fails, so no compiled pattern can ever reach regexec. Callers that
// tolerate regcomp failure (the common case) work; [[ =~ ]] reports a
// compile error instead of silently misbehaving.
int regcomp(regex_t *restrict preg, const char *restrict pattern, int flags) {
  (void)flags;
  if (!preg || !pattern)
    return REG_BADPAT;
  memset(preg, 0, sizeof(*preg));
  return REG_BADPAT;
}
