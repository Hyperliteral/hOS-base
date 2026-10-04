#include <time.h>

#include "internal/time_impl.h"

static struct tm tm_storage;

struct tm *gmtime(const time_t *t) { return gmtime_r(t, &tm_storage); }

struct tm *gmtime_r(const time_t *restrict t, struct tm *restrict tm) {
  long long days = *t / 86400;
  long long secs = *t % 86400;
  if (secs < 0) {
    secs += 86400;
    days--;
  }
  long long year;
  unsigned  mon;
  unsigned  mday;
  int       wday;
  __cavos_civil_from_days(days, &year, &mon, &mday, &wday);
  tm->tm_sec = (int)(secs % 60);
  tm->tm_min = (int)((secs / 60) % 60);
  tm->tm_hour = (int)(secs / 3600);
  tm->tm_mday = (int)mday;
  tm->tm_mon = (int)(mon - 1);
  tm->tm_year = (int)(year - 1900);
  tm->tm_wday = wday;
  // Day-of-year: ordinal of Jan 1 subtracted. 1970-01-01 is day 0, so the
  // difference of day counts gives tm_yday directly.
  tm->tm_yday =
      (int)(__cavos_days_from_civil(year, mon, mday) -
            __cavos_days_from_civil(year, 1, 1));
  tm->tm_isdst = 0;
  tm->tm_gmtoff = 0;
  tm->tm_zone = "UTC";
  return tm;
}
