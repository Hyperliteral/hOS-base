#include <regex.h>

// Unreachable through regcomp (which always fails - see regcomp.c), but
// present so binaries link. Returns NOMATCH for any direct misuse.
int regexec(const regex_t *restrict preg, const char *restrict string,
            size_t nmatch, regmatch_t pmatch[restrict], int flags) {
  (void)preg;
  (void)string;
  (void)nmatch;
  (void)pmatch;
  (void)flags;
  return REG_NOMATCH;
}
