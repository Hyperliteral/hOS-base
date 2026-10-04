#include <time.h>

// Civil calendar core shared by gmtime/localtime/mktime/strftime, all UTC.
// days_from_civil / civil_from_days are Howard Hinnant's public-domain
// algorithms: days since 1970-01-01, proleptic Gregorian.
long long __cavos_days_from_civil(long long y, unsigned m, unsigned d) {
  y -= m <= 2;
  long long era = (y >= 0 ? y : y - 399) / 400;
  unsigned  yoe = (unsigned)(y - era * 400);
  unsigned  mp = (m + 9) % 12;
  unsigned  doy = (153 * mp + 2) / 5 + d - 1;
  unsigned  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}

void __cavos_civil_from_days(long long z, long long *yp, unsigned *mp,
                             unsigned *dp, int *wday) {
  z += 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned  doe = (unsigned)(z - era * 146097);
  unsigned  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long y = (long long)yoe + era * 400;
  unsigned  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned  mp2 = (5 * doy + 2) / 153;
  unsigned  d = doy - (153 * mp2 + 2) / 5 + 1;
  unsigned  m = mp2 + 3 - 12 * (mp2 / 10);
  y += m <= 2;
  *yp = y;
  *mp = m;
  *dp = d;
  // 1970-01-01 was a Thursday (4 with Sunday == 0).
  *wday = (int)((z % 7 + 7 + 4) % 7);
}
