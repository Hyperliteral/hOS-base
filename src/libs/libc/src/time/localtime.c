#include <time.h>

// UTC-only (see tzset.c): identical to gmtime in every field.
static struct tm tm_storage;

struct tm *localtime(const time_t *t) { return localtime_r(t, &tm_storage); }

struct tm *localtime_r(const time_t *restrict t, struct tm *restrict tm) {
  return gmtime_r(t, tm);
}
