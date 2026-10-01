#include <elf.h>
#include <linux.h>
#include <serial.h>
#include <syscalls.h>
#include <types.h>

// cavOS-native syscalls - functionality that has no Linux equivalent and is
// specific to this kernel (module loading, etc). They live in the gap after
// the Mach-style port syscalls (400+).
// Copyright (C) 2024 Panagiotis

static size_t syscallModuleLoad(char *path) {
  if (!path)
    return ERR(EFAULT);

  debugf("[syscalls] module-load request: {%s}\n", path);
  if (!loadKernelModule(path))
    return ERR(ENOENT);

  return 0;
}

void syscallsRegCavos() {
  registerSyscall(SYSCALL_CAVOS_MODULE_LOAD, syscallModuleLoad);
}
