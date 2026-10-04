#include <stdlib.h>
#include <unistd.h>

// No buffered streams exist in this libc (printf writes through), so there
// is nothing to flush and no atexit handlers to run - exit is _exit.
_Noreturn void exit(int status) { _exit(status); }
