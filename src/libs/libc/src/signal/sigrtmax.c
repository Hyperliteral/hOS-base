#include <signal.h>

// Matches musl-1.2.5 (src/signal/sigrtmax.c): last signal number, so the
// realtime range is [__libc_current_sigrtmin(), _NSIG - 1].
int __libc_current_sigrtmax(void) { return _NSIG - 1; }
