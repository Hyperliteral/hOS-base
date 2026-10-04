#include <unistd.h>

// musl implements this off getrlimit too; sysconf already does exactly
// that, so delegate rather than duplicating the clamp logic.
int getdtablesize(void) {
  long v = sysconf(_SC_OPEN_MAX);
  return v < 0 ? -1 : (int)v;
}
