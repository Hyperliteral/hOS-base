#include <stdarg.h>
#include <unistd.h>

#include "internal/syscall.h"

// Direct kernel trapdoor, musl-compatible signature. All six argument slots
// are always forwarded: on x86_64 SysV the extras live in registers/stack
// the kernel simply ignores for low-arity calls, which is exactly what this
// whole libc already assumes with its raw `syscall` stubs. Portable code
// must still pass the right count - only the named slots mean anything.
long syscall(long number, ...) {
  va_list args;
  va_start(args, number);
  long a = va_arg(args, long);
  long b = va_arg(args, long);
  long c = va_arg(args, long);
  long d = va_arg(args, long);
  long e = va_arg(args, long);
  long f = va_arg(args, long);
  va_end(args);
  return __cavos_ret((unsigned long)__syscall6(number, a, b, c, d, e, f));
}
