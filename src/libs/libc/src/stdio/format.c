// Format engine shared by printf/snprintf/vsnprintf.
//
// Writes at most `capacity` bytes into `output` (no NUL is added) and returns
// the unbounded character count, so frontends implement their own termination
// and return semantics: printf transmits the bounded prefix, vsnprintf
// NUL-terminates and returns the would-be length.
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "internal/printf_impl.h"

static size_t append_text(char *output, size_t capacity, size_t position,
                          size_t *total, const char *text, size_t length,
                          int width, bool left_align, bool zero_pad) {
  if (width < 0)
    width = 0;
  size_t padding = (size_t)width > length ? (size_t)width - length : 0;
  *total += padding + length;
  if (!left_align && padding) {
    char pad = zero_pad ? '0' : ' ';
    for (size_t i = 0; i < padding && position < capacity; i++)
      output[position++] = pad;
  }
  for (size_t i = 0; i < length && position < capacity; i++)
    output[position++] = text[i];
  if (left_align) {
    for (size_t i = 0; i < padding && position < capacity; i++)
      output[position++] = ' ';
  }
  return position;
}

static size_t append_unsigned(char *output, size_t capacity, size_t position,
                              size_t *total, unsigned long long value,
                              unsigned base, bool uppercase, int width,
                              bool left_align, bool zero_pad) {
  char digits[64];
  const char *alphabet =
      uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t count = 0;
  do {
    digits[count++] = alphabet[value % base];
    value /= base;
  } while (value && count < sizeof(digits));
  char text[sizeof(digits)];
  for (size_t i = 0; i < count; i++)
    text[i] = digits[count - i - 1];
  return append_text(output, capacity, position, total, text, count, width,
                     left_align, zero_pad);
}

// Single bounded character with unbounded accounting.
static size_t emit_char(char *output, size_t capacity, size_t position,
                        size_t *total, char c) {
  if (position < capacity)
    output[position++] = c;
  (*total)++;
  return position;
}

size_t __cavos_format(char *output, size_t capacity, const char *format,
                      va_list args) {
  size_t position = 0;
  size_t total = 0;

  for (size_t index = 0; format[index]; index++) {
    if (format[index] != '%') {
      position = emit_char(output, capacity, position, &total, format[index]);
      continue;
    }
    index++;
    if (!format[index])
      break;

    bool left_align = false;
    bool zero_pad = false;
    bool plus = false;
    bool space = false;
    while (format[index] == '-' || format[index] == '0' ||
           format[index] == '+' || format[index] == ' ') {
      if (format[index] == '-')
        left_align = true;
      else if (format[index] == '0')
        zero_pad = true;
      else if (format[index] == '+')
        plus = true;
      else
        space = true;
      index++;
    }

    int width = 0;
    while (format[index] >= '0' && format[index] <= '9') {
      if (width < 10000)
        width = width * 10 + format[index] - '0';
      index++;
    }

    int precision = -1;
    if (format[index] == '.') {
      index++;
      precision = 0;
      while (format[index] >= '0' && format[index] <= '9') {
        if (precision < 10000)
          precision = precision * 10 + format[index] - '0';
        index++;
      }
    }

    int modifier = 0;
    while (format[index] == 'l' || format[index] == 'z' ||
           format[index] == 'j' || format[index] == 't') {
      if (format[index] == 'l') {
        if (modifier < 2)
          modifier++;
      } else {
        modifier = 1;
      }
      index++;
    }

    char conversion = format[index];
    if (!conversion) {
      position = emit_char(output, capacity, position, &total, '%');
      break;
    }
    if (conversion == '%') {
      position = emit_char(output, capacity, position, &total, '%');
      continue;
    }
    if (conversion == 'c') {
      char text = (char)va_arg(args, int);
      position = append_text(output, capacity, position, &total, &text, 1,
                             width, left_align, false);
      continue;
    }
    if (conversion == 's') {
      const char *text = va_arg(args, const char *);
      if (!text)
        text = "(null)";
      size_t length = strlen(text);
      if (precision >= 0 && length > (size_t)precision)
        length = (size_t)precision;
      position = append_text(output, capacity, position, &total, text, length,
                             width, left_align, false);
      continue;
    }
    if (conversion == 'd' || conversion == 'i') {
      long long value;
      if (modifier >= 2)
        value = va_arg(args, long long);
      else if (modifier == 1)
        value = va_arg(args, long);
      else
        value = va_arg(args, int);
      bool negative = value < 0;
      unsigned long long magnitude =
          negative ? (unsigned long long)(-(value + 1)) + 1
                   : (unsigned long long)value;
      char prefix = 0;
      if (negative)
        prefix = '-';
      else if (plus)
        prefix = '+';
      else if (space)
        prefix = ' ';
      if (prefix) {
        position = emit_char(output, capacity, position, &total, prefix);
        position =
            append_unsigned(output, capacity, position, &total, magnitude, 10,
                            false, width - 1, left_align, zero_pad);
      } else {
        position =
            append_unsigned(output, capacity, position, &total, magnitude, 10,
                            false, width, left_align, zero_pad);
      }
      continue;
    }
    if (conversion == 'u' || conversion == 'x' || conversion == 'X' ||
        conversion == 'o' || conversion == 'p') {
      unsigned long long value;
      int numeric_width = width;
      unsigned base = 10;
      bool uppercase = false;
      if (conversion == 'p') {
        value = (uintptr_t)va_arg(args, void *);
        position = append_text(output, capacity, position, &total, "0x", 2, 0,
                               false, false);
        if (numeric_width > 2)
          numeric_width -= 2;
        else
          numeric_width = 0;
        base = 16;
      } else if (modifier >= 2) {
        value = va_arg(args, unsigned long long);
      } else if (modifier == 1) {
        value = va_arg(args, unsigned long);
      } else {
        value = va_arg(args, unsigned int);
      }
      if (conversion == 'o')
        base = 8;
      else if (conversion == 'x' || conversion == 'X' || conversion == 'p')
        base = 16;
      uppercase = conversion == 'X';
      position = append_unsigned(output, capacity, position, &total, value,
                                 base, uppercase, numeric_width, left_align,
                                 zero_pad);
      continue;
    }
    position = emit_char(output, capacity, position, &total, '%');
    position = emit_char(output, capacity, position, &total, conversion);
  }

  return total;
}
