#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "internal/printf_impl.h"
#include "internal/stdio_impl.h"
#include "internal/syscall.h"

// Formats through the shared engine into a bounded stack buffer and writes
// the result to the stream's fd. Like printf, truncates past 2048 bytes.
int vfprintf(FILE *file, const char *format, va_list args) {
  if (!file || file->fd < 0 || (file->flags & CAVOS_F_NOWR)) {
    if (file)
      file->flags |= CAVOS_F_ERR;
    return -1;
  }
  char output[2048];
  size_t total = __cavos_format(output, sizeof(output), format, args);
  size_t transmitted = total < sizeof(output) ? total : sizeof(output);
  size_t done = 0;
  while (done < transmitted) {
    long written = __syscall3(SYS_write, file->fd, (long)(output + done),
                              (long)(transmitted - done));
    if (written <= 0) {
      file->flags |= CAVOS_F_ERR;
      return -1;
    }
    done += (size_t)written;
  }
  return (int)transmitted;
}
