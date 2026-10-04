#include <time.h>

#include "internal/time_impl.h"

// Interpreted as UTC (see tzset.c): the inverse of gmtime, normalizing
// out-of-range fields the way mktime must.
time_t mktime(struct tm *tm) {
  long long days = __cavos_days_from_civil((long long)tm->tm_year + 1900,
                                           (unsigned)tm->tm_mon + 1,
                                           (unsigned)tm->tm_mday);
  long long secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 +
                   tm->tm_sec - tm->tm_gmtoff;
  time_t result = (time_t)secs;
  // Normalize the caller's fields through the result, like musl does.
  struct tm *norm = gmtime_r(&result, tm);
  (void)norm;
  return result;
}
