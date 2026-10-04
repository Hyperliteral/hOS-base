#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/scanf_impl.h"

// Intake buffer for one numeric conversion; longer runs keep consuming (for
// span) while converting from the buffered prefix (strto* clamps).
#define SCAN_BUF 512

// Engine-level pushback stack. Conversions routinely need to return a few
// characters (float resync after strtod, inf/nan words); the backend ungetc
// is only a last-resort overflow path, so single-slot FILE pushback stays
// sufficient and user-level ungetc() semantics are untouched.
#define SCAN_STASH 16

typedef struct ScanState {
  void            *ctx;
  cavos_getc_fn    getc;
  cavos_ungetc_fn  ungetc;
  va_list          args;
  int              assigned;
  int              chars; // input characters consumed (for %n)
  int              failed;
  int              stash[SCAN_STASH];
  int              nstash;
} ScanState;

static int scan_next(ScanState *st) {
  int c;
  if (st->nstash > 0) {
    c = st->stash[--st->nstash];
  } else {
    c = st->getc(st->ctx);
    if (c == EOF)
      return EOF;
  }
  st->chars++;
  return c;
}

static void scan_backup(ScanState *st, int c) {
  if (c == EOF)
    return;
  if (st->nstash < SCAN_STASH) {
    st->stash[st->nstash++] = c;
  } else {
    st->ungetc(c, st->ctx);
  }
  st->chars--;
}

static void skip_spaces(ScanState *st) {
  int c;
  while ((c = scan_next(st)) != EOF && isspace(c))
    ;
  if (c != EOF)
    scan_backup(st, c);
}

// Store a parsed integer into the next argument according to length:
// 1=h, 2=hh, 3=l, 4=ll|q, 6=j, 7=z, 8=t (all 64-bit here), else int.
static void store_int(ScanState *st, int len, long long value,
                      int is_unsigned, unsigned long long uvalue) {
  if (len == 2) {
    signed char *p = va_arg(st->args, signed char *);
    *p = is_unsigned ? (signed char)(unsigned char)uvalue
                     : (signed char)value;
  } else if (len == 1) {
    short *p = va_arg(st->args, short *);
    *p = is_unsigned ? (short)(unsigned short)uvalue : (short)value;
  } else if (len == 3) {
    long *p = va_arg(st->args, long *);
    *p = is_unsigned ? (long)uvalue : (long)value;
  } else if (len >= 4) {
    long long *p = va_arg(st->args, long long *);
    *p = is_unsigned ? (long long)uvalue : value;
  } else {
    int *p = va_arg(st->args, int *);
    *p = is_unsigned ? (int)(unsigned)uvalue : (int)value;
  }
}

// Gather one numeric token into buf: optional sign, optional 0x prefix
// (when base allows hex), then valid digits, all bounded by width. The
// buffer always holds a complete valid prefix; the first invalid character
// is pushed back. Returns the digit count, or -1 when EOF arrived before
// anything. base==0 behaves like 16 for gathering (%i sorts out 8 vs 16
// from the buffer afterwards).
static int gather_number(ScanState *st, int width, int base, char *buf,
                         int *sign_out) {
  int vbase = base == 0 ? 16 : base;
  int pos = 0;
  int digits = 0;
  int sign = 0;
  int c = scan_next(st);
  if (c == EOF)
    return -1;
  if (c == '+' || c == '-') {
    sign = c == '-';
    if (pos < SCAN_BUF - 1)
      buf[pos++] = (char)c;
    c = scan_next(st);
  }
  if ((vbase == 16) && c == '0') {
    int c2 = scan_next(st);
    if (c2 == EOF) {
      scan_backup(st, c2);
    } else if (c2 == 'x' || c2 == 'X') {
      int c3 = scan_next(st);
      if (c3 != EOF && isxdigit(c3)) {
        if (pos < SCAN_BUF - 1)
          buf[pos++] = '0';
        if (pos < SCAN_BUF - 1)
          buf[pos++] = (char)c2;
        c = c3;
      } else {
        if (c3 != EOF)
          scan_backup(st, c3);
        scan_backup(st, c2);
      }
    } else {
      scan_backup(st, c2);
    }
  }
  for (;;) {
    int ok;
    if (vbase == 10)
      ok = c >= '0' && c <= '9';
    else if (vbase == 8)
      ok = c >= '0' && c <= '7';
    else
      ok = isxdigit(c);
    if (c == EOF || !ok || (width && digits >= width))
      break;
    if (pos < SCAN_BUF - 1)
      buf[pos++] = (char)c;
    digits++;
    c = scan_next(st);
  }
  if (c != EOF)
    scan_backup(st, c);
  buf[pos < SCAN_BUF ? pos : SCAN_BUF - 1] = 0;
  *sign_out = sign;
  return digits;
}

// Gather one float token: inf/nan words or mantissa[exponent]. The field is
// capped at SCAN_BUF-1 stored characters (longer runs truncate - documented;
// in practice only adversarial inputs notice). Every buffered character is
// re-validated by strtod, and the excess (stored minus consumed) is pushed
// back through the engine stash, so the stream always resumes exactly where
// the conversion ended. Returns 1 with the validated text in buf.
static int gather_float(ScanState *st, int width, char *buf) {
  int cap = width && width < SCAN_BUF - 1 ? width : SCAN_BUF - 1;
  int pos = 0;
  int c = scan_next(st);
  if (c == EOF)
    return 0;
  if ((c | 32) == 'i' || (c | 32) == 'n') {
    // inf/nan words: read letters, strtod decides where the word ends.
    scan_backup(st, c);
    for (int i = 0; i < cap + 4 && i < 12; i++) {
      c = scan_next(st);
      if (c == EOF)
        break;
      if (!isalpha(c) && c != '(' && c != ')' && c != '_') {
        scan_backup(st, c);
        break;
      }
      if (pos < cap)
        buf[pos++] = (char)c;
    }
    buf[pos] = 0;
    char *ep = 0;
    (void)strtod(buf, &ep);
    if (ep == buf)
      return 0; // not actually inf/nan: fail without consuming
    int taken = (int)(ep - buf);
    for (int i = pos - 1; i >= taken; i--) {
      int ch = (unsigned char)buf[i];
      if (st->nstash < SCAN_STASH) {
        st->stash[st->nstash++] = ch;
        st->chars--;
      } else {
        st->ungetc(ch, st->ctx);
        st->chars--;
      }
    }
    buf[taken] = 0;
    return 1;
  }
  scan_backup(st, c);
  int digits = 0;
  int dot = 0;
  for (;;) {
    c = scan_next(st);
    if (c == EOF)
      break;
    if (c >= '0' && c <= '9') {
      if (pos < cap)
        buf[pos++] = (char)c;
      digits++;
      continue;
    }
    if (c == '.' && !dot) {
      if (pos < cap)
        buf[pos++] = (char)c;
      dot = 1;
      continue;
    }
    break;
  }
  // Optional exponent: e/E, optional sign, at least one digit. A bare e or
  // e+ rolls everything back - it belongs to the next conversion.
  if (digits > 0 && (c == 'e' || c == 'E')) {
    int c2 = scan_next(st);
    int has_sign = c2 == '+' || c2 == '-';
    int c3 = has_sign ? scan_next(st) : c2;
    if (c3 != EOF && c3 >= '0' && c3 <= '9') {
      if (pos < cap)
        buf[pos++] = (char)c;
      if (has_sign && pos < cap)
        buf[pos++] = (char)c2;
      if (pos < cap)
        buf[pos++] = (char)c3;
      for (;;) {
        int c4 = scan_next(st);
        if (c4 == EOF)
          break;
        if (c4 < '0' || c4 > '9') {
          scan_backup(st, c4);
          break;
        }
        if (pos < cap)
          buf[pos++] = (char)c4;
        digits++;
      }
    } else {
      if (c3 != EOF)
        scan_backup(st, c3);
      if (has_sign)
        scan_backup(st, c2);
      scan_backup(st, c);
    }
  } else if (c != EOF) {
    scan_backup(st, c);
  }
  buf[pos] = 0;
  return digits > 0;
}

int __cavos_vfscanf(void *ctx, cavos_getc_fn getc, cavos_ungetc_fn ungetc,
                    const char *format, va_list args) {
  ScanState state;
  state.ctx = ctx;
  state.getc = getc;
  state.ungetc = ungetc;
  va_copy(state.args, args);
  state.assigned = 0;
  state.chars = 0;
  state.failed = 0;
  state.nstash = 0;
  ScanState *st = &state;

  for (; *format && !st->failed; format++) {
    if (isspace((unsigned char)*format)) {
      skip_spaces(st);
      continue;
    }
    if (*format != '%') {
      int c = scan_next(st);
      if (c == EOF || c != (unsigned char)*format) {
        if (c != EOF)
          scan_backup(st, c);
        st->failed = 1;
      }
      continue;
    }
    format++;
    if (*format == '%') {
      int c = scan_next(st);
      if (c == EOF || c != '%') {
        if (c != EOF)
          scan_backup(st, c);
        st->failed = 1;
      }
      continue;
    }
    int suppress = 0;
    if (*format == '*') {
      suppress = 1;
      format++;
    }
    int width = 0;
    while (isdigit((unsigned char)*format)) {
      if (width < 100000)
        width = width * 10 + (*format - '0');
      format++;
    }
    // Length: 1=h, 2=hh, 3=l, 4=ll|q, 5=L, 6=j, 7=z, 8=t.
    int len = 0;
    if (*format == 'h') {
      len = 1;
      if (format[1] == 'h') {
        len = 2;
        format++;
      }
      format++;
    } else if (*format == 'l') {
      len = 3;
      if (format[1] == 'l') {
        len = 4;
        format++;
      }
      format++;
    } else if (*format == 'q') {
      len = 4;
      format++;
    } else if (*format == 'L') {
      len = 5;
      format++;
    } else if (*format == 'j') {
      len = 6;
      format++;
    } else if (*format == 'z') {
      len = 7;
      format++;
    } else if (*format == 't') {
      len = 8;
      format++;
    }
    char conv = *format;
    if (!conv)
      break;
    if (conv == 'n') {
      if (!suppress) {
        if (len == 2)
          *va_arg(st->args, signed char *) = (signed char)st->chars;
        else if (len == 1)
          *va_arg(st->args, short *) = (short)st->chars;
        else if (len == 3)
          *va_arg(st->args, long *) = (long)st->chars;
        else if (len >= 4)
          *va_arg(st->args, long long *) = (long long)st->chars;
        else
          *va_arg(st->args, int *) = st->chars;
      }
      continue;
    }
    if (conv == 'c') {
      int w = width ? width : 1;
      if (suppress) {
        for (int i = 0; i < w; i++) {
          if (scan_next(st) == EOF) {
            st->failed = 1;
            break;
          }
        }
        if (st->failed)
          break;
        continue;
      }
      char *out = va_arg(st->args, char *);
      int   i = 0;
      for (; i < w; i++) {
        int c = scan_next(st);
        if (c == EOF)
          break;
        out[i] = (char)c;
      }
      if (i < w) {
        st->failed = 1; // short input: no assignment, values indeterminate
        break;
      }
      st->assigned++;
      continue;
    }
    if (conv == 's' || conv == '[') {
      int invert = 0;
      int table[256] = {0};
      if (conv == '[') {
        const char *p = format + 1;
        if (*p == '^') {
          invert = 1;
          p++;
        }
        if (*p == ']') {
          table[(unsigned char)']'] = 1;
          p++;
        }
        while (*p && *p != ']') {
          if (p[1] == '-' && p[2] && p[2] != ']') {
            for (int c = (unsigned char)p[0]; c <= (unsigned char)p[2]; c++)
              table[c] = 1;
            p += 3;
          } else {
            table[(unsigned char)*p] = 1;
            p++;
          }
        }
        if (!*p) {
          st->failed = 1; // unterminated set
          break;
        }
        format = p; // continue from ']'
      } else {
        skip_spaces(st);
      }
      if (suppress) {
        int got = 0;
        for (;;) {
          int c = scan_next(st);
          if (c == EOF)
            break;
          int ok = conv == 's' ? !isspace(c) : (table[c] != invert);
          if (!ok || (width && got >= width)) {
            scan_backup(st, c);
            break;
          }
          got++;
        }
        if (!got) {
          st->failed = 1;
          break;
        }
        continue;
      }
      char *out = va_arg(st->args, char *);
      int   got = 0;
      for (;;) {
        int c = scan_next(st);
        if (c == EOF)
          break;
        int ok = conv == 's' ? !isspace(c) : (table[c] != invert);
        if (!ok || (width && got >= width)) {
          scan_backup(st, c);
          break;
        }
        out[got++] = (char)c;
      }
      if (!got) {
        st->failed = 1;
        break;
      }
      out[got] = 0;
      st->assigned++;
      continue;
    }
    if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
        conv == 'x' || conv == 'X' || conv == 'p') {
      int base = 10;
      int issigned = 1;
      if (conv == 'i')
        base = 0;
      else if (conv == 'o')
        base = 8;
      else if (conv == 'x' || conv == 'X' || conv == 'p')
        base = 16;
      if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X')
        issigned = 0;
      skip_spaces(st);
      char buf[SCAN_BUF];
      int  sign = 0;
      int  digits = gather_number(st, width, base, buf, &sign);
      if (digits <= 0) {
        st->failed = 1;
        break;
      }
      // Recompute %i base: 0x-prefix means 16 (gather consumed it only
      // when hex digits followed), leading 0 means 8, else 10.
      if (conv == 'i') {
        const char *b = buf + (buf[0] == '+' || buf[0] == '-' ? 1 : 0);
        if (b[0] == '0' && (b[1] == 'x' || b[1] == 'X'))
          base = 16;
        else if (b[0] == '0' && b[1] != 0)
          base = 8;
        else
          base = 10;
      }
      if (conv == 'p') {
        unsigned long long v = strtoull(buf, 0, 16);
        if (!suppress) {
          *va_arg(st->args, void **) = (void *)(uintptr_t)v;
          st->assigned++;
        }
        continue;
      }
      if (issigned) {
        long long v = (len == 4 || len == 6) ? strtoll(buf, 0, base)
                                            : (long long)strtol(buf, 0, base);
        if (suppress)
          continue;
        store_int(st, len, v, 0, 0);
      } else {
        unsigned long long v =
            (len == 4 || len == 6) ? strtoull(buf, 0, base)
                                  : (unsigned long long)strtoul(buf, 0, base);
        if (suppress)
          continue;
        store_int(st, len, 0, 1, v);
      }
      st->assigned++;
      continue;
    }
    if (conv == 'e' || conv == 'E' || conv == 'f' || conv == 'F' ||
        conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A') {
      skip_spaces(st);
      char buf[SCAN_BUF];
      if (!gather_float(st, width, buf)) {
        st->failed = 1;
        break;
      }
      char  *ep = 0;
      double dv = strtod(buf, &ep);
      (void)ep;
      if (!suppress) {
        if (len == 5)
          *va_arg(st->args, long double *) = (long double)dv;
        else if (len == 3)
          *va_arg(st->args, double *) = dv;
        else
          *va_arg(st->args, float *) = (float)dv;
        st->assigned++;
      }
      continue;
    }
    // Unknown conversion: stop. (POSIX leaves it undefined; failing loudly
    // beats misparsing the argument list.)
    st->failed = 1;
    break;
  }

  if (!st->failed) {
    va_end(st->args);
    return st->assigned;
  }
  if (st->assigned > 0) {
    va_end(st->args);
    return st->assigned;
  }
  // Nothing converted: EOF iff the input is exhausted right now.
  int c = scan_next(st);
  if (c == EOF) {
    va_end(st->args);
    return EOF;
  }
  scan_backup(st, c);
  va_end(st->args);
  return 0;
}
