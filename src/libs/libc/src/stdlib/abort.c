#include <stdlib.h>
#include <unistd.h>

// No signals on cavOS, so SIGABRT cannot be raised the way musl does it.
// Report and exit with the conventional 134 (128 + SIGABRT) instead.
_Noreturn void abort(void) {
  const char message[] = "abort\n";
  write(STDERR_FILENO, message, sizeof(message) - 1);
  _exit(134);
}
