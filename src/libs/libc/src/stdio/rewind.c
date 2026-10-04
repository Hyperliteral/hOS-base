// rewind: seek to the start and clear both EOF and error indicators
// (unlike fseek, which preserves the error indicator). Pushback is dropped:
// a byte ungot before rewinding must not resurface afterwards.
#include <stdio.h>

#include "internal/stdio_impl.h"
#include "internal/syscall.h"

void rewind(FILE *file) {
  if (!file || file->fd < 0)
    return;
  __cavos_pushback_drop(file);
  if (__cavos_ret((unsigned long)__syscall3(SYS_lseek, file->fd, 0,
                                            SEEK_SET)) < 0) {
    file->flags |= CAVOS_F_ERR;
    return;
  }
  file->flags &= ~(CAVOS_F_EOF | CAVOS_F_ERR);
}
