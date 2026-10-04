#include <sys/mount.h>

#include "internal/syscall.h"

int mount(const char *source, const char *target, const char *fstype,
          unsigned long flags, const void *data) {
  return (int)__cavos_ret((unsigned long)__syscall5(SYS_mount, (long)source,
                                                   (long)target, (long)fstype,
                                                   (long)flags, (long)data));
}
