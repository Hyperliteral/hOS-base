#include <stdlib.h>
#include <unistd.h>

// musl-compatible stack-smashing handler. musl raises SIGABRT; with no
// signals the equivalent is a message plus the abort() exit path.
_Noreturn void __stack_chk_fail(void) {
  const char message[] = "stack smashing detected\n";
  write(STDERR_FILENO, message, sizeof(message) - 1);
  abort();
}
