#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "macho.h"

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_LSEEK 8
#define SYS_MMAP 9
#define SYS_MUNMAP 11
#define SYS_BRK 12
#define SYS_UNLINK 87
#define SYS_GETPID 39
#define SYS_GETTID 186
#define SYS_EXIT_GROUP 231

#define MACHO_PROT_READ 0x1
#define MACHO_PROT_WRITE 0x2
#define MACHO_MAP_PRIVATE 0x2
#define MACHO_MAP_FIXED 0x10
#define MACHO_MAP_ANONYMOUS 0x20
#define MACHO_O_CREAT 0x40

#define ALLOC_ALIGNMENT 16
#define ALLOC_MAGIC 0x4d41434fU

typedef struct AllocBlock {
  size_t             size;
  struct AllocBlock *next;
  uint32_t           magic;
  uint32_t           free;
  uint8_t            padding[8];
} AllocBlock;

static int       errno_value;
static uintptr_t allocator_base;
static uintptr_t allocator_break;
static AllocBlock *allocator_first;
static bool      allocator_ready;

static long cavos_syscall0(long number) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number)
                   : "rcx", "r11", "memory");
  return result;
}

static long cavos_syscall1(long number, long first) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number), "D"(first)
                   : "rcx", "r11", "memory");
  return result;
}

static long cavos_syscall2(long number, long first, long second) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number), "D"(first), "S"(second)
                   : "rcx", "r11", "memory");
  return result;
}

static long cavos_syscall3(long number, long first, long second, long third) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number), "D"(first), "S"(second), "d"(third)
                   : "rcx", "r11", "memory");
  return result;
}

static long cavos_syscall6(long number, long first, long second, long third,
                           long fourth, long fifth, long sixth) {
  long result;
  register long r10 __asm__("r10") = fourth;
  register long r8 __asm__("r8") = fifth;
  register long r9 __asm__("r9") = sixth;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number), "D"(first), "S"(second), "d"(third),
                     "r"(r10), "r"(r8), "r"(r9)
                   : "rcx", "r11", "memory");
  return result;
}

int *__errno_location(void) { return &errno_value; }
int *__error(void) { return &errno_value; }

static void set_errno_value(int value) { errno_value = value; }

static void write_all(int fd, const char *data, size_t length) {
  while (length) {
    long written = cavos_syscall3(SYS_WRITE, fd, (long)data, (long)length);
    if (written <= 0)
      return;
    size_t consumed = (size_t)written;
    if (consumed > length)
      consumed = length;
    data += consumed;
    length -= consumed;
  }
}

static size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

static void allocator_initialize(void) {
  if (allocator_ready)
    return;
  long current = cavos_syscall1(SYS_BRK, 0);
  if (current <= 0) {
    set_errno_value(12);
    return;
  }
  allocator_base = (uintptr_t)current;
  allocator_break = allocator_base;
  allocator_first = 0;
  allocator_ready = true;
}

static AllocBlock *allocator_find_free(size_t size) {
  for (AllocBlock *block = allocator_first; block; block = block->next) {
    if (!block->free || block->size < size)
      continue;
    return block;
  }
  return 0;
}

static void allocator_split(AllocBlock *block, size_t size) {
  if (size > block->size ||
      block->size - size < sizeof(AllocBlock) + ALLOC_ALIGNMENT)
    return;
  size_t remainder_size = block->size - size - sizeof(AllocBlock);
  if (remainder_size < ALLOC_ALIGNMENT)
    return;
  AllocBlock *remainder = (AllocBlock *)((uint8_t *)block + sizeof(AllocBlock) +
                                          size);
  remainder->size = remainder_size - sizeof(AllocBlock);
  remainder->next = block->next;
  remainder->magic = ALLOC_MAGIC;
  remainder->free = 1;
  block->size = size;
  block->next = remainder;
}

static void *allocator_expand(size_t size) {
  if (allocator_break > UINTPTR_MAX - size)
    return 0;
  uintptr_t start = allocator_break;
  uintptr_t end = start + size;
  long result = cavos_syscall1(SYS_BRK, (long)end);
  if (result != (long)end) {
    set_errno_value(12);
    return 0;
  }
  AllocBlock *block = (AllocBlock *)start;
  block->size = size - sizeof(AllocBlock);
  block->next = 0;
  block->magic = ALLOC_MAGIC;
  block->free = 0;
  if (!allocator_first)
    allocator_first = block;
  else {
    AllocBlock *last = allocator_first;
    while (last->next)
      last = last->next;
    last->next = block;
  }
  allocator_break = end;
  return (uint8_t *)block + sizeof(AllocBlock);
}

void *malloc(size_t size) {
  if (size == 0)
    size = 1;
  if (size > SIZE_MAX - sizeof(AllocBlock) - (ALLOC_ALIGNMENT - 1)) {
    set_errno_value(12);
    return 0;
  }
  size_t allocation_size = align_up(size, ALLOC_ALIGNMENT);
  size_t total = sizeof(AllocBlock) + allocation_size;
  if (allocation_size < size || total < allocation_size) {
    set_errno_value(12);
    return 0;
  }
  allocator_initialize();
  if (!allocator_ready)
    return 0;
  AllocBlock *block = allocator_find_free(allocation_size);
  if (block) {
    block->free = 0;
    allocator_split(block, allocation_size);
    return (uint8_t *)block + sizeof(AllocBlock);
  }
  return allocator_expand(total);
}

void free(void *pointer) {
  if (!pointer)
    return;
  allocator_initialize();
  uintptr_t address = (uintptr_t)pointer - sizeof(AllocBlock);
  if (!allocator_ready || address < allocator_base || address >= allocator_break)
    return;
  AllocBlock **link = &allocator_first;
  while (*link && *link != (AllocBlock *)address)
    link = &(*link)->next;
  AllocBlock *block = *link;
  if (!block || block->magic != ALLOC_MAGIC || block->free)
    return;
  block->free = 1;
  if (block->next && block->next->magic == ALLOC_MAGIC && block->next->free) {
    AllocBlock *next = block->next;
    block->size += sizeof(AllocBlock) + next->size;
    block->next = next->next;
  }
}

void *calloc(size_t count, size_t size) {
  if (size != 0 && count > SIZE_MAX / size) {
    set_errno_value(12);
    return 0;
  }
  size_t total = count * size;
  void *pointer = malloc(total);
  if (!pointer)
    return 0;
  uint8_t *bytes = (uint8_t *)pointer;
  for (size_t i = 0; i < total; i++)
    bytes[i] = 0;
  return pointer;
}

void *realloc(void *pointer, size_t size) {
  if (!pointer)
    return malloc(size);
  if (size == 0) {
    free(pointer);
    return 0;
  }
  if (size > SIZE_MAX - sizeof(AllocBlock) - (ALLOC_ALIGNMENT - 1)) {
    set_errno_value(12);
    return 0;
  }
  size_t allocation_size = align_up(size, ALLOC_ALIGNMENT);
  if (allocation_size < size) {
    set_errno_value(12);
    return 0;
  }
  allocator_initialize();
  uintptr_t address = (uintptr_t)pointer - sizeof(AllocBlock);
  if (!allocator_ready || address < allocator_base || address >= allocator_break)
    return 0;
  AllocBlock *block = (AllocBlock *)address;
  if (block->magic != ALLOC_MAGIC || block->free)
    return 0;
  if (block->size >= allocation_size) {
    block->free = 0;
    allocator_split(block, allocation_size);
    return pointer;
  }
  void *replacement = malloc(size);
  if (!replacement)
    return 0;
  memcpy(replacement, pointer, block->size);
  free(pointer);
  return replacement;
}

void *sbrk(intptr_t increment) {
  allocator_initialize();
  if (!allocator_ready || increment < 0)
    return (void *)-1;
  uintptr_t start = allocator_break;
  if ((uintptr_t)increment > UINTPTR_MAX - start) {
    set_errno_value(12);
    return (void *)-1;
  }
  uintptr_t end = start + (uintptr_t)increment;
  if (increment != 0 && cavos_syscall1(SYS_BRK, (long)end) != (long)end) {
    set_errno_value(12);
    return (void *)-1;
  }
  allocator_break = end;
  return (void *)start;
}

long read(int fd, void *buffer, size_t count) {
  return cavos_syscall3(SYS_READ, fd, (long)buffer, (long)count);
}

long write(int fd, const void *buffer, size_t count) {
  return cavos_syscall3(SYS_WRITE, fd, (long)buffer, (long)count);
}

int open(const char *path, int flags, ...) {
  long mode = 0;
  if (flags & MACHO_O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }
  return (int)cavos_syscall3(SYS_OPEN, (long)path, flags, mode);
}

int close(int fd) { return (int)cavos_syscall1(SYS_CLOSE, fd); }

long lseek(int fd, long offset, int whence) {
  return cavos_syscall3(SYS_LSEEK, fd, offset, whence);
}

int unlink(const char *path) {
  return (int)cavos_syscall1(SYS_UNLINK, (long)path);
}

int isatty(int fd) {
  (void)fd;
  return 0;
}

long syscall_getpid(void) { return cavos_syscall0(SYS_GETPID); }
long syscall_gettid(void) { return cavos_syscall0(SYS_GETTID); }

void *mmap(void *address, size_t length, int protection, int flags, int fd,
           long offset) {
  return (void *)cavos_syscall6(SYS_MMAP, (long)address, (long)length,
                                protection, flags, fd, offset);
}

int munmap(void *address, size_t length) {
  return (int)cavos_syscall2(SYS_MUNMAP, (long)address, (long)length);
}

void *brk(void *address) {
  allocator_initialize();
  if (!allocator_ready)
    return (void *)-1;
  if (!address)
    return (void *)allocator_break;
  uintptr_t target = (uintptr_t)address;
  if (target < allocator_break || target - allocator_break > INTPTR_MAX)
    return (void *)-1;
  size_t increment = (size_t)(target - allocator_break);
  if (increment != 0 && sbrk((intptr_t)increment) == (void *)-1)
    return (void *)-1;
  return address;
}

char **environ;

char *getenv(const char *name) {
  if (!environ || !name)
    return 0;
  size_t length = strlen(name);
  for (char **entry = environ; *entry; entry++) {
    if (strncmp(*entry, name, length) == 0 && (*entry)[length] == '=')
      return *entry + length + 1;
  }
  return 0;
}

int atoi(const char *text) {
  if (!text)
    return 0;
  size_t index = 0;
  int sign = 1;
  if (text[index] == '-' || text[index] == '+') {
    if (text[index] == '-')
      sign = -1;
    index++;
  }
  int value = 0;
  while (text[index] >= '0' && text[index] <= '9') {
    value = value * 10 + text[index] - '0';
    index++;
  }
  return value * sign;
}

void *memcpy(void *destination, const void *source, size_t length) {
  uint8_t *out = (uint8_t *)destination;
  const uint8_t *in = (const uint8_t *)source;
  for (size_t i = 0; i < length; i++)
    out[i] = in[i];
  return destination;
}

void *memmove(void *destination, const void *source, size_t length) {
  uint8_t *out = (uint8_t *)destination;
  const uint8_t *in = (const uint8_t *)source;
  if (out == in || length == 0)
    return destination;
  if (out < in) {
    for (size_t i = 0; i < length; i++)
      out[i] = in[i];
  } else {
    for (size_t i = length; i > 0; i--)
      out[i - 1] = in[i - 1];
  }
  return destination;
}

void *memset(void *destination, int value, size_t length) {
  uint8_t *out = (uint8_t *)destination;
  uint8_t byte = (uint8_t)value;
  for (size_t i = 0; i < length; i++)
    out[i] = byte;
  return destination;
}

int memcmp(const void *left, const void *right, size_t length) {
  const uint8_t *a = (const uint8_t *)left;
  const uint8_t *b = (const uint8_t *)right;
  for (size_t i = 0; i < length; i++) {
    if (a[i] != b[i])
      return a[i] < b[i] ? -1 : 1;
  }
  return 0;
}

void *memchr(const void *value, int character, size_t length) {
  const uint8_t *bytes = (const uint8_t *)value;
  uint8_t byte = (uint8_t)character;
  for (size_t i = 0; i < length; i++) {
    if (bytes[i] == byte)
      return (void *)(bytes + i);
  }
  return 0;
}

size_t strlen(const char *text) {
  size_t length = 0;
  while (text[length])
    length++;
  return length;
}

char *strcpy(char *destination, const char *source) {
  size_t i = 0;
  while (source[i]) {
    destination[i] = source[i];
    i++;
  }
  destination[i] = '\0';
  return destination;
}

char *strncpy(char *destination, const char *source, size_t length) {
  size_t i = 0;
  while (i < length && source[i]) {
    destination[i] = source[i];
    i++;
  }
  while (i < length)
    destination[i++] = '\0';
  return destination;
}

char *strcat(char *destination, const char *source) {
  size_t offset = strlen(destination);
  size_t i = 0;
  while (source[i]) {
    destination[offset + i] = source[i];
    i++;
  }
  destination[offset + i] = '\0';
  return destination;
}

char *strncat(char *destination, const char *source, size_t length) {
  size_t offset = strlen(destination);
  size_t i = 0;
  while (i < length && source[i]) {
    destination[offset + i] = source[i];
    i++;
  }
  destination[offset + i] = '\0';
  return destination;
}

int strcmp(const char *left, const char *right) {
  size_t i = 0;
  while (left[i] && left[i] == right[i])
    i++;
  return (unsigned char)left[i] - (unsigned char)right[i];
}

int strncmp(const char *left, const char *right, size_t length) {
  size_t i = 0;
  while (i < length && left[i] && left[i] == right[i])
    i++;
  if (i == length)
    return 0;
  return (unsigned char)left[i] - (unsigned char)right[i];
}

char *strchr(const char *text, int character) {
  uint8_t byte = (uint8_t)character;
  for (size_t i = 0;; i++) {
    if ((uint8_t)text[i] == byte)
      return (char *)(text + i);
    if (!text[i])
      return 0;
  }
}

char *strrchr(const char *text, int character) {
  uint8_t byte = (uint8_t)character;
  char *result = 0;
  for (size_t i = 0;; i++) {
    if ((uint8_t)text[i] == byte)
      result = (char *)(text + i);
    if (!text[i])
      return result;
  }
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle)
    return (char *)haystack;
  size_t needle_length = strlen(needle);
  for (; *haystack; haystack++) {
    if (strncmp(haystack, needle, needle_length) == 0)
      return (char *)haystack;
  }
  return 0;
}

static size_t append_text(char *output, size_t capacity, size_t position,
                           const char *text, size_t length, int width,
                           bool left_align, bool zero_pad) {
  if (width < 0)
    width = 0;
  size_t padding = (size_t)width > length ? (size_t)width - length : 0;
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
                              unsigned long long value, unsigned base,
                              bool uppercase, int width, bool left_align,
                              bool zero_pad) {
  char digits[64];
  const char *alphabet = uppercase ? "0123456789ABCDEF"
                                   : "0123456789abcdef";
  size_t count = 0;
  do {
    digits[count++] = alphabet[value % base];
    value /= base;
  } while (value && count < sizeof(digits));
  char text[sizeof(digits)];
  for (size_t i = 0; i < count; i++)
    text[i] = digits[count - i - 1];
  return append_text(output, capacity, position, text, count, width, left_align,
                     zero_pad);
}

int printf(const char *format, ...) {
  char output[2048];
  size_t position = 0;
  va_list args;
  va_start(args, format);

  for (size_t index = 0; format[index] && position < sizeof(output); index++) {
    if (format[index] != '%') {
      if (position < sizeof(output))
        output[position++] = format[index];
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
      if (position < sizeof(output))
        output[position++] = '%';
      break;
    }
    if (conversion == '%') {
      if (position < sizeof(output))
        output[position++] = '%';
      continue;
    }
    if (conversion == 'c') {
      char text = (char)va_arg(args, int);
      position = append_text(output, sizeof(output), position, &text, 1, width,
                             left_align, false);
      continue;
    }
    if (conversion == 's') {
      const char *text = va_arg(args, const char *);
      if (!text)
        text = "(null)";
      size_t length = strlen(text);
      if (precision >= 0 && length > (size_t)precision)
        length = (size_t)precision;
      position = append_text(output, sizeof(output), position, text, length, width,
                             left_align, false);
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
        position = append_text(output, sizeof(output), position, &prefix, 1, 0,
                               false, false);
        position = append_unsigned(output, sizeof(output), position, magnitude, 10,
                                   false, width - 1, left_align, zero_pad);
      } else {
        position = append_unsigned(output, sizeof(output), position, magnitude, 10,
                                   false, width, left_align, zero_pad);
      }
      continue;
    }
    if (conversion == 'u' || conversion == 'x' || conversion == 'X' ||
        conversion == 'o' || conversion == 'p') {
      unsigned long long value;
      int numeric_width = width;
      if (conversion == 'p') {
        value = (uintptr_t)va_arg(args, void *);
        position = append_text(output, sizeof(output), position, "0x", 2, 0,
                               false, false);
        if (numeric_width > 2)
          numeric_width -= 2;
        else
          numeric_width = 0;
      } else if (modifier >= 2) {
        value = va_arg(args, unsigned long long);
      } else if (modifier == 1) {
        value = va_arg(args, unsigned long);
      } else {
        value = va_arg(args, unsigned int);
      }
      unsigned base = conversion == 'o' ? 8 : 16;
      position = append_unsigned(output, sizeof(output), position, value, base,
                                 conversion == 'X', numeric_width, left_align,
                                 zero_pad);
      continue;
    }
    if (position < sizeof(output))
      output[position++] = '%';
    if (position < sizeof(output))
      output[position++] = conversion;
  }

  va_end(args);
  write_all(1, output, position);
  return (int)position;
}

int puts(const char *text) {
  size_t length = strlen(text);
  write_all(1, text, length);
  write_all(1, "\n", 1);
  return (int)length;
}

int putchar(int character) {
  char byte = (char)character;
  write_all(1, &byte, 1);
  return character;
}

_Noreturn void exit(int status) {
  cavos_syscall1(SYS_EXIT_GROUP, status);
  __builtin_unreachable();
}

_Noreturn void _Exit(int status) { exit(status); }

_Noreturn void abort(void) {
  write_all(2, "abort\n", 6);
  exit(134);
}
