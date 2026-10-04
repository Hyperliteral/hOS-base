// Thin cavOS wrapper layer over the vendored raw syscall stubs.
//
// musl routes every syscall through its pthread/cancellation machinery
// (src/internal/syscall.h pulls in the thread pointer), which this libc does
// not implement. Instead each wrapper calls __syscallN directly and funnels
// the result through __cavos_ret(), which applies the Linux convention the
// kernel follows: -4095..-1 means -errno.
//
// Syscall numbers come from <sys/syscall.h>, which is part of the vendored
// musl header tree and matches the cavOS kernel's Linux-compatible table.
#ifndef CAVOS_INTERNAL_SYSCALL_H
#define CAVOS_INTERNAL_SYSCALL_H

#include <errno.h>
#include <sys/syscall.h>

#include "syscall_arch.h"

// Map a raw kernel return value to the libc convention: -1 with errno set
// on failure, the value itself otherwise.
static __inline long __cavos_ret(unsigned long result) {
  if (result > -4096UL) {
    *__errno_location() = (int)-(long)result;
    return -1;
  }
  return (long)result;
}

#endif
