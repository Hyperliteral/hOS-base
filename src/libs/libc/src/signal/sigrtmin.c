#include <signal.h>

// Values match musl-1.2.5 (src/signal/sigrtmin.c): first usable realtime
// signal on Linux. See sigrtmax.c for the top end.
int __libc_current_sigrtmin(void) { return 35; }
