// UTC civil-calendar core (see src/time/civil.c). Howard Hinnant's
// public-domain algorithms; shared by gmtime/localtime/mktime/strftime.
#ifndef CAVOS_INTERNAL_TIME_IMPL_H
#define CAVOS_INTERNAL_TIME_IMPL_H

long long __cavos_days_from_civil(long long y, unsigned m, unsigned d);
void __cavos_civil_from_days(long long z, long long *yp, unsigned *mp,
                             unsigned *dp, int *wday);

#endif
