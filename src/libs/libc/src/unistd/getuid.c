#include <sys/types.h>
#include <unistd.h>

#include "internal/syscall.h"

uid_t getuid(void) {
  return (uid_t)__cavos_ret((unsigned long)__syscall0(SYS_getuid));
}
