// Dynamic loading on top of the program interpreter.
//
// dlopen() maps a library through the same machinery boot uses (resolve,
// map, relocate, bind, init); dlsym() searches it. RTLD_LAZY binds eagerly
// like everything else here (documented); RTLD_LOCAL/GLOBAL both behave as
// GLOBAL because the single global scope is all there is; dlclose() never
// unloads (consistent with __cxa_finalize's assumption). dlerror() reports
// the last failure from a process-global buffer.
#include <dlfcn.h>
#include <elf.h>
#include <stdlib.h>
#include <string.h>

#include "internal/ldso_impl.h"
#include "internal/syscall.h"

static char dl_error[256];

static void set_error(const char *what, const char *detail) {
  size_t pos = 0;
  while (*what && pos + 1 < sizeof(dl_error))
    dl_error[pos++] = *what++;
  if (detail) {
    if (pos + 2 < sizeof(dl_error)) {
      dl_error[pos++] = ':';
      dl_error[pos++] = ' ';
    }
    while (*detail && pos + 1 < sizeof(dl_error))
      dl_error[pos++] = *detail++;
  }
  dl_error[pos] = 0;
}

char *dlerror(void) {
  static char last[256];
  if (!dl_error[0])
    return 0;
  strcpy(last, dl_error);
  dl_error[0] = 0;
  return last;
}

// Scope array for one lookup: up to every loaded object plus self.
#define DL_SCOPE_MAX (8 + 2)

static size_t build_scope(CavosObj **scope, CavosObj *first) {
  size_t n = 0;
  for (CavosObj *o = first; o && n < DL_SCOPE_MAX - 1; o = o->next)
    scope[n++] = o;
  if (__cavos_dl_self) {
    size_t i = 0;
    for (; i < n; i++) {
      if (scope[i] == __cavos_dl_self)
        break;
    }
    if (i == n)
      scope[n++] = __cavos_dl_self;
  }
  return n;
}

void *dlopen(const char *path, int flags) {
  (void)flags; // LAZY == NOW here; LOCAL == GLOBAL (single scope)
  if (!__cavos_dl_head || !__cavos_dl_self) {
    set_error("dynamic loader not initialized", 0);
    return 0;
  }
  if (!path) {
    // dlopen(NULL): handle for the global scope (resolved via RTLD_DEFAULT
    // in dlsym, but also a valid passthrough here).
    return RTLD_DEFAULT;
  }
  int       is_new = 0;
  CavosObj *lib =
      __cavos_load_library(__cavos_dl_head, __cavos_dl_self, path, &is_new);
  if (!lib) {
    set_error("cannot load library", path);
    return 0;
  }
  if (!is_new)
    return lib; // already loaded and bound
  // Static TLS froze at startup: a late PT_TLS has nowhere to go (no DTV
  // slot, no per-thread area for it). Refuse loudly like musl's "TLS
  // storage exhausted" instead of handing out a dangling block.
  if (lib->phdr && lib->phnum) {
    const Elf64_Phdr *ph = (const Elf64_Phdr *)lib->phdr;
    for (size_t i = 0; i < lib->phnum; i++) {
      if (ph[i].p_type == 7 /* PT_TLS */ && ph[i].p_memsz) {
        set_error("TLS in dlopened libraries unsupported", path);
        return 0;
      }
    }
  }
  // Append, guarding the scope array bound (boot enforces the same limit).
  size_t    depth = 0;
  CavosObj *tail = __cavos_dl_head;
  for (CavosObj *o = tail; o; o = o->next)
    depth++;
  if (depth + 1 >= DL_SCOPE_MAX) {
    set_error("too many loaded libraries", path);
    return 0;
  }
  while (tail->next)
    tail = tail->next;
  tail->next = lib;
  CavosObj *scope[DL_SCOPE_MAX];
  size_t    nscope = build_scope(scope, __cavos_dl_head);
  if (__cavos_bind_object(lib, scope, nscope, 1, 0) ||
      __cavos_bind_object(lib, scope, nscope, 0, 0) ||
      __cavos_bind_object(lib, scope, nscope, 0, 1)) {
    set_error("cannot bind library", path);
    return 0;
  }
  if (lib->init)
    ((void (*)(void))lib->init)();
  for (size_t i = 0; i + sizeof(uintptr_t) <= lib->init_arraysz;
       i += sizeof(uintptr_t)) {
    uintptr_t fn = lib->base + *(uintptr_t *)(lib->init_array + i);
    if (fn != lib->base)
      ((void (*)(void))fn)();
  }
  return lib;
}

void *dlsym(void *restrict handle, const char *restrict name) {
  if (!name) {
    set_error("dlsym with NULL symbol", 0);
    return 0;
  }
  CavosObj *scope[DL_SCOPE_MAX];
  size_t    nscope = 0;
  if (handle == RTLD_DEFAULT || handle == 0) {
    nscope = build_scope(scope, __cavos_dl_head);
  } else if (handle == RTLD_NEXT) {
    // Search objects after the caller's: locate the calling object by
    // return address containment, then scope everything past it (plus
    // self, which is never in the chain). Falls back to every library
    // when the caller cannot be placed (hand-written asm callers).
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    CavosObj *after = 0;
    for (CavosObj *o = __cavos_dl_head; o; o = o->next) {
      if (caller >= o->base && caller - o->base < o->span)
        after = o;
    }
    if (!after && __cavos_dl_self && caller >= __cavos_dl_self->base &&
        caller - __cavos_dl_self->base < __cavos_dl_self->span) {
      after = __cavos_dl_self;
    }
    CavosObj *start = 0;
    if (after == __cavos_dl_self) {
      start = 0; // caller is libc itself: everything after it is fair game,
                 // which in chain terms is the whole chain (self excluded,
                 // re-added below).
    } else if (after) {
      start = after->next;
    } else {
      start = __cavos_dl_head ? __cavos_dl_head->next : 0;
    }
    for (CavosObj *o = start; o && nscope < DL_SCOPE_MAX - 1; o = o->next)
      scope[nscope++] = o;
    if (__cavos_dl_self && nscope < DL_SCOPE_MAX)
      scope[nscope++] = __cavos_dl_self;
  } else {
    nscope = build_scope(scope, (CavosObj *)handle);
  }
  size_t    size = 0;
  uintptr_t def = __cavos_lookup(scope, nscope, name, 0, &size);
  (void)size;
  if (!def) {
    set_error("undefined symbol", name);
    return 0;
  }
  return (void *)def;
}

int dlclose(void *handle) {
  (void)handle;
  // Never unloads: init/fini accounting (__cxa_finalize) and the missing
  // munmap path both assume permanence. Returns success, like a nodelete.
  return 0;
}
