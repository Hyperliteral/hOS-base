#include <string.h>
#include <time.h>

#include "internal/time_impl.h"

// C-locale strftime. Composites (%c %x %X %r %R %T %D %F) expand through the
// same engine recursively; %s needs epoch seconds, carried alongside.
static const char *const week_days[] = {
  "Sunday", "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday",
};

static const char *const months[] = {
  "January", "February", "March", "April", "May", "June", "July", "August",
  "September", "October", "November", "December",
};

typedef struct StrfState {
  char  *out;
  size_t max;
  size_t pos;
  size_t total;
  int    overflow;
} StrfState;

static void emit(StrfState *st, char c) {
  if (st->pos < st->max)
    st->out[st->pos] = c;
  if (st->pos < st->max)
    st->pos++;
  else
    st->overflow = 1;
  st->total++;
}

static void emit_str(StrfState *st, const char *s) {
  while (*s)
    emit(st, *s++);
}

static void emit_n(StrfState *st, const char *s, size_t n) {
  for (size_t i = 0; i < n; i++)
    emit(st, s[i]);
}

// Zero- (or space-) padded decimal, up to 4 digits plus sign.
static void emit_num(StrfState *st, long value, int w, char pad) {
  char digits[16];
  int  n = 0;
  long v = value < 0 ? -value : value;
  if (value < 0)
    emit(st, '-');
  do {
    digits[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v && n < (int)sizeof(digits));
  while (n < w) {
    emit(st, pad);
    w--;
  }
  while (n--)
    emit(st, digits[n]);
}

// ISO week (%V) and its year (%G): Thursday of this week decides the year.
static void iso_week(const struct tm *tm, int *week, int *year) {
  int year_now = tm->tm_year + 1900;
  int wday_mon0 = (tm->tm_wday + 6) % 7;
  int thursday = tm->tm_yday - wday_mon0 + 3;
  int leap = (year_now % 4 == 0 &&
              (year_now % 100 != 0 || year_now % 400 == 0));
  int ydays = leap ? 366 : 365;
  int out_year = year_now;
  if (thursday < 0) {
    out_year--;
    int prev_leap = (out_year % 4 == 0 &&
                     (out_year % 100 != 0 || out_year % 400 == 0));
    thursday += prev_leap ? 366 : 365;
  } else if (thursday >= ydays) {
    thursday -= ydays;
    out_year++;
  }
  *week = thursday / 7 + 1;
  *year = out_year;
}

static int weekday(const struct tm *tm) {
  return tm->tm_wday >= 0 && tm->tm_wday < 7 ? tm->tm_wday : 0;
}

static int month(const struct tm *tm) {
  return tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0;
}

static void format_inner(StrfState *st, const char *fmt, const struct tm *tm,
                         long long epoch_secs);

static void format_one(StrfState *st, char conv, const struct tm *tm,
                       long long epoch_secs) {
  int year = tm->tm_year + 1900;
  int hour12 = tm->tm_hour % 12;
  if (hour12 == 0)
    hour12 = 12;
  switch (conv) {
  case 'Y': emit_num(st, year, 4, '0'); break;
  case 'y': emit_num(st, year % 100, 2, '0'); break;
  case 'C': emit_num(st, year / 100, 2, '0'); break;
  case 'm': emit_num(st, tm->tm_mon + 1, 2, '0'); break;
  case 'd': emit_num(st, tm->tm_mday, 2, '0'); break;
  case 'e': emit_num(st, tm->tm_mday, 2, ' '); break;
  case 'H': emit_num(st, tm->tm_hour, 2, '0'); break;
  case 'k': emit_num(st, tm->tm_hour, 2, ' '); break;
  case 'I': emit_num(st, hour12, 2, '0'); break;
  case 'l': emit_num(st, hour12, 2, ' '); break;
  case 'M': emit_num(st, tm->tm_min, 2, '0'); break;
  case 'S': emit_num(st, tm->tm_sec, 2, '0'); break;
  case 'j': emit_num(st, tm->tm_yday + 1, 3, '0'); break;
  case 'w': emit_num(st, tm->tm_wday, 1, '0'); break;
  case 'u': emit_num(st, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0'); break;
  case 'U': {
    int jan1 = (tm->tm_wday - tm->tm_yday % 7 + 700) % 7;
    int first = (7 - jan1) % 7;
    emit_num(st, tm->tm_yday < first ? 0 : (tm->tm_yday - first) / 7 + 1, 2,
             '0');
    break;
  }
  case 'W': {
    int jan1 = (tm->tm_wday - tm->tm_yday % 7 + 700) % 7;
    int jan1m = (jan1 + 6) % 7;
    int first = (7 - jan1m) % 7;
    emit_num(st, tm->tm_yday < first ? 0 : (tm->tm_yday - first) / 7 + 1, 2,
             '0');
    break;
  }
  case 'V':
  case 'G':
  case 'g': {
    int week;
    int isoyear;
    iso_week(tm, &week, &isoyear);
    if (conv == 'V')
      emit_num(st, week, 2, '0');
    else if (conv == 'G')
      emit_num(st, isoyear, 4, '0');
    else
      emit_num(st, isoyear % 100, 2, '0');
    break;
  }
  case 'a': emit_n(st, week_days[weekday(tm)], 3); break;
  case 'A': emit_str(st, week_days[weekday(tm)]); break;
  case 'b':
  case 'h': emit_n(st, months[month(tm)], 3); break;
  case 'B': emit_str(st, months[month(tm)]); break;
  case 'p': emit_str(st, tm->tm_hour < 12 ? "AM" : "PM"); break;
  case 'Z': emit_str(st, tm->tm_zone ? tm->tm_zone : "UTC"); break;
  case 'z': emit_str(st, "+0000"); break;
  case 's': {
    // Epoch seconds, like %Y unbounded by width.
    char digits[24];
    int  n = 0;
    long long v = epoch_secs < 0 ? -epoch_secs : epoch_secs;
    if (epoch_secs < 0)
      emit(st, '-');
    do {
      digits[n++] = (char)('0' + v % 10);
      v /= 10;
    } while (v && n < (int)sizeof(digits));
    while (n--)
      emit(st, digits[n]);
    break;
  }
  case 'c': format_inner(st, "%a %b %e %H:%M:%S %Y", tm, epoch_secs); break;
  case 'x': format_inner(st, "%m/%d/%y", tm, epoch_secs); break;
  case 'X': format_inner(st, "%H:%M:%S", tm, epoch_secs); break;
  case 'r': format_inner(st, "%I:%M:%S %p", tm, epoch_secs); break;
  case 'R': format_inner(st, "%H:%M", tm, epoch_secs); break;
  case 'T': format_inner(st, "%H:%M:%S", tm, epoch_secs); break;
  case 'D': format_inner(st, "%m/%d/%y", tm, epoch_secs); break;
  case 'F': format_inner(st, "%Y-%m-%d", tm, epoch_secs); break;
  case '%': emit(st, '%'); break;
  default:
    // Unknown conversion: emit literally (% plus letter), like musl does
    // for most extensions rather than failing the whole string.
    emit(st, '%');
    emit(st, conv);
    break;
  }
}

static void format_inner(StrfState *st, const char *fmt, const struct tm *tm,
                         long long epoch_secs) {
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      emit(st, *fmt);
      continue;
    }
    fmt++;
    if (!*fmt)
      break;
    format_one(st, *fmt, tm, epoch_secs);
  }
}

size_t strftime(char *restrict s, size_t max, const char *restrict format,
                const struct tm *restrict tm) {
  if (!max)
    return 0;
  // Epoch for %s: invert the UTC fields (mktime would normalize a copy;
  // the direct computation avoids touching the caller's struct).
  long long epoch_secs =
      __cavos_days_from_civil((long long)tm->tm_year + 1900,
                              (unsigned)tm->tm_mon + 1,
                              (unsigned)tm->tm_mday) *
          86400LL +
      tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec -
      tm->tm_gmtoff;
  StrfState state;
  state.out = s;
  state.max = max - 1; // reserve the NUL
  state.pos = 0;
  state.total = 0;
  state.overflow = 0;
  format_inner(&state, format, tm, epoch_secs);
  if (state.overflow)
    return 0;
  s[state.pos] = 0;
  return state.pos;
}
