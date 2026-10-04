#include <sys/utsname.h>

#include "internal/syscall.h"

int uname(struct utsname *name) {
  return (int)__cavos_ret((unsigned long)__syscall1(SYS_uname, (long)name));
}
