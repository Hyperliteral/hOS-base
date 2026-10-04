// Minimal printf: formats into a bounded buffer, then writes it through.
// No FILE machinery (musl's stdio.h stays vendored for declarations, but the
// buffered-stream half is not implemented - see README.md).
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "internal/printf_impl.h"
#include "internal/syscall.h"

static void write_all(int fd, const char *data, size_t length) {
  while (length) {
    long written = __syscall3(SYS_write, fd, (long)data, (long)length);
    if (written <= 0)
      return;
    size_t consumed = (size_t)written;
    if (consumed > length)
      consumed = length;
    data += consumed;
    length -= consumed;
  }
}

int printf(const char *format, ...) {
  char output[2048];
  va_list args;
  va_start(args, format);
  size_t total = __cavos_format(output, sizeof(output), format, args);
  va_end(args);
  size_t transmitted = total < sizeof(output) ? total : sizeof(output);
  write_all(1, output, transmitted);
  return (int)transmitted;
}
