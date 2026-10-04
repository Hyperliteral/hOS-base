#include <unistd.h>

// No setuid execution model exists here (single-user embedded target), so
// no process is ever tainted: always 0. If setuid binaries ever land, this
// must consult AT_SECURE state instead.
int issetugid(void) { return 0; }
