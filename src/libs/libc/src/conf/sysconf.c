#include <errno.h>
#include <limits.h>
#include <sys/resource.h>
#include <unistd.h>

// Queried values only; anything exotic fails with EINVAL like musl does
// for unknown names. Counts come from live kernel state where one exists.
long sysconf(int name) {
  switch (name) {
  case _SC_PAGESIZE:
    return 4096;
  case _SC_OPEN_MAX: {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
      return -1;
    if (rl.rlim_cur > (rlim_t)LONG_MAX)
      return LONG_MAX;
    return (long)rl.rlim_cur;
  }
  case _SC_NPROCESSORS_ONLN:
  case _SC_NPROCESSORS_CONF:
    return 1; // no affinity/topology reporting exists yet
  case _SC_CLK_TCK:
    return 100;
  case _SC_GETPW_R_SIZE_MAX:
  case _SC_GETGR_R_SIZE_MAX:
    return 1024;
  case _SC_ARG_MAX: {
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) < 0)
      return -1;
    rlim_t v = rl.rlim_cur / 4 * 3;
    if (v > (rlim_t)LONG_MAX)
      return LONG_MAX;
    return (long)v;
  }
  case _SC_CHILD_MAX:
  case _SC_STREAM_MAX:
  case _SC_TZNAME_MAX:
    return 64;
  case _SC_VERSION:
    return _POSIX_VERSION;
  default:
    *__errno_location() = EINVAL;
    return -1;
  }
}
