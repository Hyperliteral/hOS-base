// Internal program-interpreter (ld.so) interfaces.
//
// The interpreter entry (src/ldso/entry.asm) self-relocates the library,
// then calls __cavos_ldso_boot(), which loads the executable's NEEDED chain,
// binds every relocation and jumps to the executable entry. Only used on the
// ELF path: dynamic binaries (INTERP) cannot run without it, static ones
// never invoke it.
#ifndef CAVOS_INTERNAL_LDSO_IMPL_H
#define CAVOS_INTERNAL_LDSO_IMPL_H

#include <elf.h>
#include <stddef.h>
#include <stdint.h>

// One loaded object: the executable or a NEEDED library. Addresses labeled
// "link" are link-time values (file content); those labeled "run" already
// include the load base.
// One PT_LOAD, for the post-bind protection pass.
typedef struct CavosLoad {
  uintptr_t vaddr;
  size_t    memsz;
  unsigned  flags;
} CavosLoad;

typedef struct CavosObj {
  const char     *name; // SONAME or path, for diagnostics
  // Load bias: runtime address of link-address 0, i.e. map address minus the
  // first LOAD's vaddr. All link-time addresses become runtime by adding
  // this. (Equals the map address for standard files whose first LOAD sits
  // at vaddr 0, but executables routinely start higher - see the probe with
  // its DYNAMIC at 0x202500.)
  uintptr_t       base;
  const Elf64_Sym *dynsym;
  size_t          nsyms; // from DT_HASH (SYSV); all our libs carry one
  const char     *strtab;
  // Link-time DYNAMIC values, rebased on use:
  uintptr_t       rela;
  size_t          relasz;
  uintptr_t       jmprel;
  size_t          pltrelsz;
  uintptr_t       relr;
  size_t          relrsz;
  int             pltrel; // DT_PLTREL value; only RELA supported
  uintptr_t       init;
  uintptr_t       init_array;
  size_t          init_arraysz;
  uintptr_t       dyn; // runtime address of this object's DYNAMIC table
  const char     *soname; // DT_SONAME (malloc'd copy), or 0
  uintptr_t       phdr; // runtime address of this object's PHDR list (0 if none)
  size_t          phnum;
  size_t          span;   // coverage end: callers in [base, base+span)
  CavosLoad      *loads;  // PT_LOAD list (loaded libs only), for protection
  size_t          nloads;
  struct CavosObj *next; // NEEDED breadth-first load order
} CavosObj;

// Entered from entry.asm with our load base and the entry stack pointer.
// Parses the stack, loads/binds everything, jumps to the executable entry.
// Never returns; exits the process (127) with a diagnostic on failure.
void __cavos_ldso_boot(uintptr_t base, void *sp);

// Loaded-object chain for dlopen()/dlsym(): exe-anchored NEEDED
// breadth-first list (__cavos_dl_head) plus ourselves (__cavos_dl_self,
// deliberately outside the chain - see boot.c). Published once boot
// completes; dlfcn.c is the only other consumer.
extern CavosObj *__cavos_dl_head;
extern CavosObj *__cavos_dl_self;

// Minimal thread pointer for foreign (musl-built) binaries.
// Idempotent: safe to call on both the static and dynamic paths.
void __cavos_setup_tls(void);

// Loader core shared with dlfcn.c (all defined in boot.c):
uintptr_t __cavos_find_dynamic(const Elf64_Phdr *phdrs, size_t phnum,
                               uintptr_t base);
void __cavos_parse_dynamic(CavosObj *obj, uintptr_t dyn);
uintptr_t __cavos_lookup(CavosObj **scope, size_t nscope, const char *name,
                         CavosObj *skip, size_t *size_out);
int  __cavos_bind_object(CavosObj *obj, CavosObj **scope, size_t nscope,
                         int relative_only, int copy_only);
int  __cavos_open_needed(const char *name);
CavosObj *__cavos_load_library(CavosObj *head, CavosObj *self,
                               const char *name, int *is_new);

#endif
