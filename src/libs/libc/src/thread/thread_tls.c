// Thread control block and static TLS (ELF).
//
// Layout follows Drepper's static model on x86_64: one contiguous area per
// thread holding every initially-loaded module's TLS template, with the TCB
// at the top and TP pointing at it. Link-time `x@tpoff` offsets are negative
// from TP, so module 0 (the executable, registered first) sits lowest.
// Dynamic (GD/LD) access goes through __tls_get_addr over a per-thread DTV
// that shares one offset table.
//
// Registration happens once, single-threaded, before any thread exists:
//   - static binaries: __libc_start_main parses auxv AT_PHDR and registers
//     the executable's PT_TLS, then installs the main thread;
//   - dynamic binaries: the ldso boot registers the exe and each NEEDED
//     library it loads (same order => same offsets), then installs.
// Libraries pulled in later via dlopen() with a PT_TLS are refused with an
// error instead of silently corrupting TLS (matches musl's "TLS storage
// exhausted" failure, just earlier and louder). See src/ldso/dlfcn.c.
//
// The Mach-O path never touches this file's image code: that target cannot
// even compile __thread (host clang has no darwin TLS backend), so crt0
// installs a bare TCB via __cavos_setup_tls and pthread keys/TSD cover
// thread-local storage there. Same pthread API, same TCB, no image.
#include <elf.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "internal/pthread_impl.h"
#include "internal/syscall.h"

#ifndef PT_TLS
#define PT_TLS 7
#endif

#define TLS_MAX_MODULES 8
// TCB placement needs 8-byte alignment (pointers/atomics inside); module
// packing below follows p_align *exactly* - the static linker assigns
// `x@tpoff` from the same packing, so any extra minimum alignment here
// would shift template contents away from the link-time offsets (seen live:
// a forced 16 against p_align 4 moved every variable by 8).
#define TCB_ALIGN 8

extern uintptr_t __stack_chk_guard;

typedef struct {
  uintptr_t template_addr; // runtime address of the file image (tdata)
  size_t    memsz;
  size_t    filesz;
  size_t    align;
  size_t    offset; // offset from the static area base
} TlsModule;

static TlsModule tls_modules[TLS_MAX_MODULES];
static size_t    tls_count;
static size_t    tls_total; // exact span of module packing (linker-identical)
static size_t    tls_padded; // span padded so TP (span end) is TCB-aligned
static int       tls_ready; // layout frozen once the first thread installs

static size_t align_up(size_t v, size_t a) {
  if (a <= 1)
    return v;
  return (v + a - 1) & ~(a - 1);
}

long __cavos_tls_register(uintptr_t addr, size_t memsz, size_t filesz,
                          size_t align) {
  if (tls_ready || tls_count >= TLS_MAX_MODULES || !memsz)
    return -1;
  if (align <= 1)
    align = 1;
  size_t off = align_up(tls_total, align);
  tls_modules[tls_count].template_addr = addr;
  tls_modules[tls_count].memsz = memsz;
  tls_modules[tls_count].filesz = filesz > memsz ? memsz : filesz;
  tls_modules[tls_count].align = align;
  tls_modules[tls_count].offset = off;
  tls_total = off + memsz;
  return (long)++tls_count; // 1-based module id (DTV[0] is reserved)
}

// Freeze: pad the span so TP (= area base + padded span) is TCB-aligned.
// Module offsets shift by the same pad, so TP-relative addresses - and
// hence every link-time tpoff - are unchanged.
static void tls_freeze(void) {
  if (tls_ready)
    return;
  tls_padded = align_up(tls_total, TCB_ALIGN);
  size_t pad = tls_padded - tls_total;
  if (pad) {
    for (size_t i = 0; i < tls_count; i++)
      tls_modules[i].offset += pad;
  }
  tls_ready = 1;
}

size_t __cavos_tls_static_size(void) {
  tls_freeze();
  return tls_padded;
}

// Fill one thread's static area (base) from the registered templates.
static void tls_fill(void *base) {
  for (size_t i = 0; i < tls_count; i++) {
    char *dst = (char *)base + tls_modules[i].offset;
    memcpy(dst, (const void *)tls_modules[i].template_addr,
           tls_modules[i].filesz);
    memset(dst + tls_modules[i].filesz,
           0, tls_modules[i].memsz - tls_modules[i].filesz);
  }
}

// Install TP for a TCB sitting at the top of its area. The self word
// itself is set by setup_common on every path: the TP read (%fs:0) returns
// the self word, so a TCB without self set reads back NULL.
static void tls_point(struct __pthread *t) {
  __cavos_ret((unsigned long)__syscall2(SYS_arch_prctl, 0x1002, (long)t));
}

static uintptr_t mix_canary(uintptr_t base, int tid) {
  uintptr_t v = base ^ (uintptr_t)(tid * 0x9e3779b9);
  v ^= v >> 29;
  v *= 0xbf58476d1ce4e5b9ULL;
  v ^= v >> 32;
  return v ? v : base;
}

static struct __pthread *alloc_tcb(size_t extra_below) {
  size_t            area = align_up(extra_below, TCB_ALIGN);
  size_t            total = area + sizeof(struct __pthread);
  struct __pthread *t = calloc(1, total);
  if (!t)
    return 0;
  return (struct __pthread *)((char *)t + area);
}

static void setup_common(struct __pthread *t, void *area, int tid,
                         int install_tp) {
  tls_freeze(); // finalize offsets before the first fill
  tls_fill(area);
  t->self = t;
  t->tid = tid;
  t->errno_val = 0;
  t->detach_state = CAVOS_DT_JOINABLE;
  t->canary = mix_canary(__stack_chk_guard, tid);
  t->dtv_len = tls_count + 1;
  if (tls_count) {
    t->dtv = calloc(tls_count + 1, sizeof(void *));
    if (t->dtv) {
      t->dtv[0] = (void *)1; // generation, nonzero
      for (size_t i = 0; i < tls_count; i++)
        t->dtv[i + 1] = (void *)((char *)area + tls_modules[i].offset);
    }
  } else {
    t->dtv = 0;
  }
  // install_tp: the main thread self-installs here; new threads must NOT
  // (the kernel installs their TP from CLONE_SETTLS at clone time - doing
  // it here would repoint the *parent's* FS at the child's TCB).
  if (install_tp)
    tls_point(t);
}

// Register every PT_TLS in one object's PHDR list. `bias` rebases p_vaddr to
// a runtime address (0 for a non-PIE executable, the load bias otherwise).
void __cavos_tls_register_image(const void *phdr, size_t phnum,
                                uintptr_t bias) {
  const Elf64_Phdr *p = phdr;
  for (size_t i = 0; i < phnum; i++) {
    if (p[i].p_type != PT_TLS || p[i].p_memsz == 0)
      continue;
    // The template lives inside a LOAD segment the loader already mapped;
    // p_vaddr (+bias) is its runtime address by construction.
    __cavos_tls_register(bias + p[i].p_vaddr, p[i].p_memsz, p[i].p_filesz,
                         p[i].p_align);
  }
}

// Install the main thread's TCB + static area. Registration must already be
// done (images registered above); this freezes the layout.
void __cavos_install_main_tcb(void) {
  size_t              area = __cavos_tls_static_size();
  struct __pthread   *t = alloc_tcb(area);
  if (!t)
    return; // hopeless; caller proceeds without threads (errno falls back)
  setup_common(t, (char *)t - area, (int)__syscall0(SYS_gettid), 1);
}

void __cavos_init_main_tls(const void *phdr, size_t phnum, uintptr_t bias) {
  if (phdr && phnum)
    __cavos_tls_register_image(phdr, phnum, bias);
  __cavos_install_main_tcb();
}

// Bare TCB for contexts with no TLS image (Mach-O crt0, ldso pre-parse).
// Preserves the old __cavos_setup_tls contract: self at +0, canary at +0x28.
void __cavos_setup_tls(void) {
  struct __pthread *t = calloc(1, sizeof(*t));
  if (!t)
    return;
  t->self = t;
  t->canary = __stack_chk_guard;
  t->detach_state = CAVOS_DT_JOINABLE;
  t->tid = (int)__syscall0(SYS_gettid);
  tls_point(t);
}

// Per-thread area carved from the thread's own stack mapping. Must NOT
// install TP (see setup_common): the kernel does that from CLONE_SETTLS.
void __cavos_tls_init_thread(struct __pthread *t, void *static_base,
                             size_t static_size) {
  (void)static_size;
  setup_common(t, static_base, t->tid, 0);
}

// Compiler entry for global-dynamic / local-dynamic TLS:
// v = {module id, offset}. Modules are 1-based; 0 is never valid.
void *__tls_get_addr(size_t *v) {
  size_t m = v[0];
  size_t o = v[1];
  struct __pthread *t = __cavos_self();
  if (!t || m == 0 || m > tls_count || !t->dtv || !t->dtv[m])
    return 0;
  return (char *)t->dtv[m] + o;
}
