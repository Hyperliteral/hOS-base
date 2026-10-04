#include <time.h>

// No timezone data exists here (no tzdata, no /etc/localtime parsing), so
// the zone is permanently UTC and tzset() only records the call. localtime
// therefore equals gmtime - documented, and exactly right twice a day in
// Berlin.
void tzset(void) {}
