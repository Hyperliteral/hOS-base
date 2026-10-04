#include <stdlib.h>
#include <unistd.h>

// musl spelling of _exit, kept next to exit for the same reason.
_Noreturn void _Exit(int status) { _exit(status); }
