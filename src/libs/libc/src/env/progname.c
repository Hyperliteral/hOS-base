// program_invocation_name / program_invocation_short_name plus the musl
// __progname / __progname_full cells (referenced by Alpine binaries even
// though the vendored headers do not declare them).
//
// musl fills these from argv[0] in its startup code. This libc must not edit
// its entry stub, so the exec path is recovered with getauxval(AT_EXECFN)
// (contract mechanism: AT_EXECFN=31, parse /proc/self/auxv pairs until
// AT_NULL, basename after the last '/'). A constructor initializes the cells
// at load; the globals are pre-pointed at a static buffer so direct readers
// never see NULL even if the constructor has not run (falling back to "?").
#include <elf.h>
#include <stddef.h>
#include <sys/auxv.h>

static char prog_buf[512] = "?";

char *program_invocation_name = prog_buf;
char *program_invocation_short_name = prog_buf;
char *__progname = prog_buf;
char *__progname_full = prog_buf;

static void basename_of(const char *path, const char **base) {
  const char *last = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/')
      last = p + 1;
  }
  *base = last;
}

__attribute__((constructor)) static void progname_init(void) {
  unsigned long execfn = getauxval(AT_EXECFN);
  if (!execfn)
    return;
  const char *path = (const char *)execfn;
  size_t i = 0;
  while (i < sizeof(prog_buf) - 1 && path[i]) {
    prog_buf[i] = path[i];
    i++;
  }
  prog_buf[i] = '\0';
  if (i == 0)
    return;
  const char *base;
  basename_of(prog_buf, &base);
  program_invocation_name = prog_buf;
  __progname_full = prog_buf;
  program_invocation_short_name = (char *)base;
  __progname = (char *)base;
}
