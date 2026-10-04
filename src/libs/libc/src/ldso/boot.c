// Minimal ELF program interpreter (dynamic linker).
//
// Entered once per dynamic exec via _dlstart (src/ldso/entry.asm), which has
// already self-relocated and self-bound this library, so every cavOS libc
// function is callable here.
//
// What it does, in order:
//   1. Parses the entry stack (argc/argv/envp/auxv, all standard Linux).
//   2. Refuses nested interpreters and handles direct execution of our own
//      file (prints a version line instead of looping on itself).
//   3. Loads the executable's NEEDED chain breadth-first: absolute SONAMEs
//      open as-is, bare names search CAVOS_LD_PATH (debug/testing only),
//      then /lib/ and /usr/lib/. Every object is mapped ANON + memcpy'd,
//      so no MAP_FIXED games and no reliance on kernel address choice.
//   4. Applies RELATIVE (+RELR bitmap) relocs to every object, then symbolic
//      ones (GLOB_DAT/JUMP_SLOT/ABS64/COPY) with a linear dynsym scan over
//      the global scope. Unresolvable symbols are REPORTED, not crashed on.
//   5. Runs loaded libraries' INIT/INIT_ARRAY (not the exe's - its own
//      _start handles those through __libc_start_main).
//   6. Jumps to the executable entry on the pristine stack.
//
// Deliberate limits (all diagnosed, never silent):
//   - No TLS/threads: a TLS relocation aborts the exec with a message.
//   - No symbol versioning: musl and our own toolchain emit none.
//   - No DT_SYMBOLIC/DF_TEXTREL-style exotic dynamic tags.
//   - RELRO segments are left writable (no mprotect downgrade pass).
//   - The first LOAD of every object must start at vaddr 0 (true for all
//     lld/GNU-ld output); bases are derived from AT_PHDR on that basis.
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "internal/impl.h"
#include "internal/ldso_impl.h"
#include "internal/pthread_impl.h"
#include "internal/syscall.h"

extern uintptr_t __stack_chk_guard;

#define LDSO_VERSION_LINE "hmake-ldso (cavOS libc program interpreter)\n"

// Load-address policy for NEEDED libraries: let the kernel choose (mmap with
// a NULL hint), so no fixed bases can collide with the interpreter, the
// executable, or the brk heap. At most LDSO_LIB_MAX libraries per process.
#define LDSO_LIB_MAX 8

static void ldso_write(int fd, const char *s) {
  size_t left = strlen(s);
  while (left) {
    long wrote =
        __syscall3(SYS_write, fd, (long)(s + (strlen(s) - left)), (long)left);
    if (wrote <= 0)
      break;
    left -= (size_t)wrote;
  }
}

// Set from CAVOS_LD_DEBUG in boot: milestone tracing for loader debugging
// (entry, argc, loads, bind counts, jump). Off unless requested.
static int ld_debug;

#define DBG(s)                                                             \
  do {                                                                     \
    if (ld_debug)                                                          \
      ldso_write(2, s);                                                    \
  } while (0)

static void dbg_ulong(unsigned long v) {
  if (!ld_debug)
    return;
  char digits[24];
  int  i = 0;
  if (!v)
    digits[i++] = '0';
  while (v && (size_t)i < sizeof(digits) - 1) {
    digits[i++] = (char)('0' + v % 10);
    v /= 10;
  }
  // Reverse in place, then write once.
  for (int a = 0, b = i - 1; a < b; a++, b--) {
    char t = digits[a];
    digits[a] = digits[b];
    digits[b] = t;
  }
  digits[i] = 0;
  ldso_write(2, digits);
}

static _Noreturn void ldso_fail(const char *what, const char *detail) {
  ldso_write(2, "hmake-ldso: ");
  ldso_write(2, what);
  if (detail) {
    ldso_write(2, ": ");
    ldso_write(2, detail);
  }
  ldso_write(2, "\n");
  __syscall1(SYS_exit_group, 127);
  __builtin_unreachable();
}

typedef struct Auxv {
  uintptr_t phdr;
  size_t    phnum;
  size_t    phent;
  uintptr_t entry;
  void     *random;
} Auxv;

void __cavos_parse_stack(void *sp, char ***argv_out, char ***envp_out,
                        Auxv *aux) {
  uintptr_t *s = (uintptr_t *)sp;
  int        argc = (int)s[0];
  char     **argv = (char **)(s + 1);
  char     **envp = argv + argc + 1;
  *argv_out = argv;
  *envp_out = envp;
  aux->phdr = 0;
  aux->phnum = 0;
  aux->phent = 0;
  aux->entry = 0;
  aux->random = 0;
  uintptr_t *a = (uintptr_t *)envp;
  while (*a)
    a++;
  for (a++; a[0] != AT_NULL; a += 2) {
    switch (a[0]) {
    case AT_PHDR: aux->phdr = a[1]; break;
    case AT_PHNUM: aux->phnum = a[1]; break;
    case AT_PHENT: aux->phent = a[1]; break;
    case AT_ENTRY: aux->entry = a[1]; break;
    case AT_RANDOM: aux->random = (void *)a[1]; break;
    default: break;
    }
  }
}

// The executable's PHDRs are already mapped at their runtime address
// (AT_PHDR); ours and each library's live at base + e_phoff in the file or
// mapping. Both cases reduce to "walk PHDRs at some address".
// First LOAD vaddr of a PHDR list (file offset 0 lives there). Standard
// files use 0; executables may start higher.
uintptr_t __cavos_first_load_vaddr(const Elf64_Phdr *phdrs, size_t phnum) {
  for (size_t i = 0; i < phnum; i++) {
    if (phdrs[i].p_type == PT_LOAD)
      return phdrs[i].p_vaddr;
  }
  ldso_fail("object without LOAD segments", 0);
  __builtin_unreachable();
}

uintptr_t __cavos_find_dynamic(const Elf64_Phdr *phdrs, size_t phnum,
                              uintptr_t base) {
  for (size_t i = 0; i < phnum; i++) {
    // PT_INTERP is deliberately ignored: the top executable carries one by
    // definition (it names us), and a library carrying one is still ours
    // to load - we are the interpreter either way.
    if (phdrs[i].p_type == PT_DYNAMIC)
      return base + phdrs[i].p_vaddr;
  }
  return 0;
}

// Symbol count from a GNU hash table (no SYSV DT_HASH present, as in
// Alpine binaries). nchain is not stored, so walk every bucket chain to its
// LSB terminator and take the highest index + 1. Well-formed linker output
// always terminates; linear lookup afterwards needs no bloom.
size_t __cavos_gnu_nchain(uintptr_t ghash) {
  const uint32_t *h = (const uint32_t *)ghash;
  uint32_t        nbuckets = h[0];
  uint32_t        symoffset = h[1];
  uint32_t        bloom_size = h[2];
  const uint32_t *buckets =
      (const uint32_t *)((const uint64_t *)(h + 4) + bloom_size);
  const uint32_t *chain = buckets + nbuckets;
  size_t          max = symoffset ? (size_t)symoffset - 1 : 0;
  for (uint32_t b = 0; b < nbuckets; b++) {
    uint32_t i = buckets[b];
    if (!i)
      continue;
    for (;; i++) {
      if ((size_t)i > max)
        max = i;
      if (chain[i - symoffset] & 1)
        break;
    }
  }
  return max + 1;
}

void __cavos_parse_dynamic(CavosObj *obj, uintptr_t dyn) {
  obj->dyn = dyn;
  obj->soname = 0;
  obj->dynsym = 0;
  obj->nsyms = 0;
  obj->strtab = 0;
  obj->rela = 0;
  obj->relasz = 0;
  obj->jmprel = 0;
  obj->pltrelsz = 0;
  obj->relr = 0;
  obj->relrsz = 0;
  obj->init = 0;
  obj->init_array = 0;
  obj->init_arraysz = 0;
  uintptr_t hash = 0;
  uintptr_t gnu_hash = 0;
  obj->pltrel = DT_RELA; // default when the tag is absent
  for (const Elf64_Dyn *d = (const Elf64_Dyn *)dyn; d->d_tag != DT_NULL; d++) {
    switch (d->d_tag) {
    case DT_PLTREL: obj->pltrel = (int)d->d_un.d_val; break;
    case DT_GNU_HASH: gnu_hash = obj->base + d->d_un.d_val; break;
    case DT_SYMTAB: obj->dynsym = (const Elf64_Sym *)(obj->base + d->d_un.d_val); break;
    case DT_HASH: hash = obj->base + d->d_un.d_val; break;
    case DT_STRTAB: obj->strtab = (const char *)(obj->base + d->d_un.d_val); break;
    case DT_RELA: obj->rela = obj->base + d->d_un.d_val; break;
    case DT_RELASZ: obj->relasz = d->d_un.d_val; break;
    case DT_JMPREL: obj->jmprel = obj->base + d->d_un.d_val; break;
    case DT_PLTRELSZ: obj->pltrelsz = d->d_un.d_val; break;
    case DT_RELR: obj->relr = obj->base + d->d_un.d_val; break;
    case DT_RELRSZ: obj->relrsz = d->d_un.d_val; break;
    case DT_INIT: obj->init = obj->base + d->d_un.d_val; break;
    case DT_INIT_ARRAY: obj->init_array = obj->base + d->d_un.d_val; break;
    case DT_INIT_ARRAYSZ: obj->init_arraysz = d->d_un.d_val; break;
    default: break;
    }
  }
  // Second pass for SONAME: tag order is unspecified, and STRTAB may come
  // after it. The copy is malloc'd - the source strtab can belong to a file
  // buffer that is freed while the chain lives forever.
  for (const Elf64_Dyn *d = (const Elf64_Dyn *)dyn; d->d_tag != DT_NULL; d++) {
    if (d->d_tag != DT_SONAME)
      continue;
    const char *s = obj->strtab + d->d_un.d_val;
    char       *copy = malloc(strlen(s) + 1);
    if (copy)
      strcpy(copy, s);
    obj->soname = copy;
  }
  if (!obj->dynsym || !obj->strtab || (!hash && !gnu_hash))
    ldso_fail("object without hash/symbols", obj->name);
  if (hash) {
    obj->nsyms = ((const uint32_t *)hash)[1]; // DT_HASH[1] = nchain
  } else {
    obj->nsyms = __cavos_gnu_nchain(gnu_hash);
    if (!obj->nsyms)
      ldso_fail("object with empty GNU hash", obj->name);
  }
}

// Global-scope linear lookup. WEAK definitions lose to GLOBAL ones; LOCAL,
// undefined and empty-named symbols never match. Absolute (SHN_ABS)
// definitions are returned as-is, everything else rebased. Returns 0 when
// nothing matches; optionally reports the definition size (for COPY).
uintptr_t __cavos_lookup(CavosObj **scope, size_t nscope, const char *name,
                        CavosObj *skip, size_t *size_out) {
  uintptr_t weak = 0;
  size_t    weak_size = 0;
  for (size_t s = 0; s < nscope; s++) {
    CavosObj *obj = scope[s];
    if (obj == skip)
      continue;
    for (size_t i = 1; i < obj->nsyms; i++) {
      const Elf64_Sym *sym = &obj->dynsym[i];
      if ((sym->st_info >> 4) == STB_LOCAL)
        continue;
      if (sym->st_shndx == SHN_UNDEF)
        continue;
      if (strcmp(obj->strtab + sym->st_name, name) != 0)
        continue;
      uintptr_t addr = sym->st_shndx == SHN_ABS
                           ? (uintptr_t)sym->st_value
                           : obj->base + sym->st_value;
      if ((sym->st_info >> 4) == STB_GLOBAL) {
        if (size_out)
          *size_out = sym->st_size;
        return addr;
      }
      if (!weak) {
        weak = addr;
        weak_size = sym->st_size;
      }
    }
  }
  if (size_out)
    *size_out = weak_size;
  return weak;
}

int __cavos_is_tls_type(uint32_t type) {
  return type == R_X86_64_DTPMOD64 || type == R_X86_64_DTPOFF64 ||
         type == R_X86_64_TPOFF64 || type == R_X86_64_DTPOFF32 ||
         type == R_X86_64_TPOFF32 || type == R_X86_64_TLSDESC;
}

// Apply one RELA-style relocation. Returns 1 when a diagnostic was printed
// (unresolvable or unsupported), 0 otherwise.
int __cavos_apply_rela_one(CavosObj *obj, const Elf64_Rela *r,
                          CavosObj **scope, size_t nscope) {
  uint32_t type = (uint32_t)r->r_info;
  uintptr_t where = obj->base + r->r_offset;
  if (type == R_X86_64_NONE)
    return 0;
  if (type == R_X86_64_RELATIVE) {
    *(uintptr_t *)where = obj->base + r->r_addend;
    return 0;
  }
  if (__cavos_is_tls_type(type) || type == R_X86_64_IRELATIVE) {
    ldso_write(2, "hmake-ldso: unsupported relocation (TLS/ifunc) in ");
    ldso_write(2, obj->name);
    ldso_write(2, "\n");
    return 1;
  }
  if (type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT &&
      type != R_X86_64_64 && type != R_X86_64_COPY) {
    ldso_write(2, "hmake-ldso: unsupported relocation type in ");
    ldso_write(2, obj->name);
    ldso_write(2, "\n");
    return 1;
  }
  uint32_t    symidx = (uint32_t)(r->r_info >> 32);
  if (symidx >= obj->nsyms) {
    ldso_write(2, "hmake-ldso: relocation with out-of-range symbol in ");
    ldso_write(2, obj->name);
    ldso_write(2, "\n");
    return 1;
  }
  const Elf64_Sym *ref = &obj->dynsym[symidx];
  // A WEAK undefined reference with no definition anywhere resolves to
  // address 0 (plus addend), so `if (func)` guards work. This is how
  // _ITM_* and similar optional ABI normally settle - not an error.
  int        ref_weak = (ref->st_info >> 4) == STB_WEAK &&
                        ref->st_shndx == SHN_UNDEF;
  const char *symname = obj->strtab + ref->st_name;
  if (type == R_X86_64_COPY) {
    // COPY only makes sense against other objects; the symbol is UND here.
    size_t    size = 0;
    uintptr_t from = __cavos_lookup(scope, nscope, symname, obj, &size);
    if (!from || !size) {
      if (ref_weak)
        return 0; // nothing to copy, nothing to report
      ldso_write(2, "hmake-ldso: cannot COPY ");
      ldso_write(2, symname);
      ldso_write(2, " for ");
      ldso_write(2, obj->name);
      ldso_write(2, "\n");
      return 1;
    }
    DBG("ldso: copy ");
    DBG(symname);
    DBG(" dst=");
    dbg_ulong(where);
    DBG(" from=");
    dbg_ulong(from);
    DBG(" size=");
    dbg_ulong(size);
    DBG("\n");
    memcpy((void *)where, (const void *)from, size);
    return 0;
  }
  size_t    size = 0;
  uintptr_t def = __cavos_lookup(scope, nscope, symname, 0, &size);
  (void)size;
  if (!def) {
    if (ref_weak) {
      *(uintptr_t *)where = r->r_addend;
      return 0;
    }
    ldso_write(2, "hmake-ldso: cannot resolve ");
    ldso_write(2, symname);
    ldso_write(2, " (needed by ");
    ldso_write(2, obj->name);
    ldso_write(2, ")\n");
    return 1;
  }
  *(uintptr_t *)where = def + r->r_addend;
  return 0;
}

// RELR bitmap decoding: even entries set the running address AND relocate
// their own word (run starts always need it - verified); odd entries are
// bitmaps where bit i (1..63) relocates word i; the cursor advances 63
// words afterwards. Bit 0 is the type flag only. All three facts were
// determined empirically against lld-produced tables (wrong by even one
// word and real binaries die) - see the design notes.
void __cavos_apply_relr(CavosObj *obj) {
  if (!obj->relr || !obj->relrsz)
    return;
  uintptr_t       *entry = (uintptr_t *)obj->relr;
  uintptr_t       *end = (uintptr_t *)(obj->relr + obj->relrsz);
  volatile uintptr_t *where = 0;
  for (; entry < end; entry++) {
    uintptr_t e = *entry;
    if ((e & 1) == 0) {
      where = (volatile uintptr_t *)(obj->base + e);
      *where += obj->base;
    } else {
      if (!where)
        ldso_fail("RELR bitmap without base", obj->name);
      for (unsigned i = 1; i < 64; i++) {
        if ((e >> i) & 1)
          where[i] += obj->base;
      }
      where += 63;
    }
  }
}

int __cavos_bind_object(CavosObj *obj, CavosObj **scope, size_t nscope,
                       int relative_only, int copy_only) {
  int missing = 0;
  if (!relative_only && !copy_only)
    __cavos_apply_relr(obj);
  for (const Elf64_Rela *r = (const Elf64_Rela *)obj->rela,
                        *end = (const Elf64_Rela *)(obj->rela + obj->relasz);
       r < end; r++) {
    uint32_t type = (uint32_t)r->r_info;
    int is_relative =
        type == R_X86_64_NONE || type == R_X86_64_RELATIVE;
    int is_copy = type == R_X86_64_COPY;
    if (relative_only != is_relative)
      continue;
    // COPY reads relocated CONTENT (not just symbol values), so it runs
    // in its own pass after every other symbolic relocation landed.
    if (!relative_only && (is_copy != copy_only))
      continue;
    missing |= __cavos_apply_rela_one(obj, r, scope, nscope);
  }
  if (!relative_only && !copy_only) {
    for (const Elf64_Rela *r = (const Elf64_Rela *)obj->jmprel,
                          *end =
                              (const Elf64_Rela *)(obj->jmprel + obj->pltrelsz);
         r < end; r++)
      missing |= __cavos_apply_rela_one(obj, r, scope, nscope);
  }
  return missing;
}

int __cavos_open_path(const char *path) {
  return (int)__cavos_ret((unsigned long)__syscall4(
      SYS_openat, AT_FDCWD, (long)path, O_RDONLY, (long)0));
}

// Resolve a DT_NEEDED name to a file: absolute names open as-is, bare names
// search CAVOS_LD_PATH (colon list, testing/debug only), then /lib/ and
// /usr/lib/. Returns an open fd or -1.
int __cavos_open_needed(const char *name) {
  if (strchr(name, '/'))
    return __cavos_open_path(name);
  const char *extra = getenv("CAVOS_LD_PATH");
  char        candidate[512];
  if (extra && *extra) {
    for (const char *dir = extra;;) {
      size_t dir_len = strcspn(dir, ":");
      if (dir_len + 1 + strlen(name) < sizeof(candidate)) {
        memcpy(candidate, dir, dir_len);
        candidate[dir_len] = '/';
        strcpy(candidate + dir_len + 1, name);
        int fd = __cavos_open_path(candidate);
        if (fd >= 0)
          return fd;
      }
      dir += dir_len;
      if (!*dir)
        break;
      dir++;
    }
  }
  static const char *const sysdirs[] = { "/lib/", "/usr/lib/", 0 };
  for (size_t i = 0; sysdirs[i]; i++) {
    size_t prefix = strlen(sysdirs[i]);
    if (prefix + strlen(name) >= sizeof(candidate))
      continue;
    strcpy(candidate, sysdirs[i]);
    strcpy(candidate + prefix, name);
    int fd = __cavos_open_path(candidate);
    if (fd >= 0)
      return fd;
  }
  return -1;
}

long __cavos_read_all(int fd, void *buffer, size_t length) {
  size_t done = 0;
  while (done < length) {
    long got = __cavos_ret((unsigned long)__syscall3(
        SYS_read, fd, (long)((char *)buffer + done), (long)(length - done)));
    if (got < 0)
      return got;
    if (got == 0)
      break;
    done += (size_t)got;
  }
  return (long)done;
}

// SONAME straight from a file image. Link addresses equal file offsets
// here (first LOAD at vaddr 0), so DYNAMIC d_vals index the buffer directly.
const char * __cavos_file_soname(const uint8_t *file) {
  const Elf64_Ehdr *e = (const Elf64_Ehdr *)file;
  const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(file + e->e_phoff);
  for (size_t i = 0; i < e->e_phnum; i++) {
    if (phdrs[i].p_type != PT_DYNAMIC)
      continue;
    const Elf64_Dyn *dyn = (const Elf64_Dyn *)(file + phdrs[i].p_offset);
    const char     *strtab = 0;
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
      if (d->d_tag == DT_STRTAB)
        strtab = (const char *)(file + d->d_un.d_val);
    }
    if (!strtab)
      return 0;
    for (const Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
      if (d->d_tag == DT_SONAME)
        return strtab + d->d_un.d_val;
    }
    return 0;
  }
  return 0;
}

// Map one NEEDED library file: anonymous mapping sized to the LOAD span,
// segments copied in, BSS already zero, per-segment mprotect applied.
// Libraries stay mapped for the life of the process. When the file's SONAME
// is already loaded (including ourselves - the common case for libc itself,
// reached through symlinks and alternate spellings), the existing object is
// returned and nothing is mapped twice.
CavosObj *__cavos_load_library(CavosObj *head, CavosObj *self,
                              const char *name, int *is_new) {
  int fd = __cavos_open_needed(name);
  if (fd < 0)
    ldso_fail("cannot load NEEDED library", name);
  long size = __cavos_ret(
      (unsigned long)__syscall3(SYS_lseek, fd, 0, SEEK_END));
  if (size <= 0)
    ldso_fail("cannot size NEEDED library", name);
  __cavos_ret((unsigned long)__syscall3(SYS_lseek, fd, 0, SEEK_SET));
  uint8_t *file = malloc((size_t)size);
  if (!file || __cavos_read_all(fd, file, (size_t)size) != size)
    ldso_fail("cannot read NEEDED library", name);
  __syscall1(SYS_close, fd);

  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)file;
  if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
      ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F' ||
      ehdr->e_type != ET_DYN)
    ldso_fail("not a shared object", name);
  const char *soname = __cavos_file_soname(file);
  if (self->soname && soname && strcmp(self->soname, soname) == 0) {
    // This IS us (symlinked musl names, alternate spellings): reuse the
    // already relocated self instead of mapping a duplicate libc.
    free(file);
    *is_new = 0;
    return self;
  }
  for (CavosObj *o = head; o; o = o->next) {
    if (o->soname && soname && strcmp(o->soname, soname) == 0) {
      free(file);
      *is_new = 0;
      return o;
    }
  }
  *is_new = 1;
  const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(file + ehdr->e_phoff);
  uintptr_t         lo = (uintptr_t)-1;
  uintptr_t         hi = 0;
  for (size_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD)
      continue;
    if (phdrs[i].p_vaddr < lo)
      lo = phdrs[i].p_vaddr;
    if (phdrs[i].p_vaddr + phdrs[i].p_memsz > hi)
      hi = phdrs[i].p_vaddr + phdrs[i].p_memsz;
  }
  if (lo == (uintptr_t)-1)
    ldso_fail("NEEDED library without LOAD segments", name);
  size_t span = hi - lo;
  void  *map = (void *)__cavos_ret((unsigned long)__syscall6(
      SYS_mmap, 0, (long)span, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (!map || map == MAP_FAILED)
    ldso_fail("cannot map NEEDED library", name);
  uintptr_t run = (uintptr_t)map;
  uintptr_t bias = run - lo;
  for (size_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD)
      continue;
    memcpy((void *)(bias + phdrs[i].p_vaddr), file + phdrs[i].p_offset,
           phdrs[i].p_filesz);
  }
  // DYNAMIC link address from the file image (EHDR is at file offset 0 by
  // definition), rebased onto the mapping.
  uintptr_t file_dyn = 0;
  {
    const Elf64_Ehdr *fehdr = (const Elf64_Ehdr *)file;
    const Elf64_Phdr *fphdrs = (const Elf64_Phdr *)(file + fehdr->e_phoff);
    for (size_t i = 0; i < fehdr->e_phnum; i++) {
      if (fphdrs[i].p_type == PT_DYNAMIC) {
        file_dyn = fphdrs[i].p_vaddr;
        break;
      }
    }
  }
  if (!file_dyn)
    ldso_fail("NEEDED library has no DYNAMIC", name);

  // Record the LOAD list for the post-bind protection pass below.
  // Protection MUST wait until binding completes: RELRO-covered GOTs are
  // writable right now and the binder writes them. (The kernel itself maps
  // everything RW, so skipping protection entirely would still run - just
  // without NX/RO hardening.)
  size_t    nloads = 0;
  for (size_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdrs[i].p_type == PT_LOAD)
      nloads++;
  }
  CavosLoad *loads = malloc(nloads * sizeof(*loads));
  if (!loads)
    ldso_fail("out of memory loading", name);
  size_t li = 0;
  for (size_t i = 0; i < ehdr->e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD)
      continue;
    loads[li].vaddr = phdrs[i].p_vaddr;
    loads[li].memsz = phdrs[i].p_memsz;
    loads[li].flags = phdrs[i].p_flags;
    li++;
  }
  free(file);

  DBG("ldso: load ");
  DBG(name);
  DBG("\n");
  CavosObj *obj = malloc(sizeof(*obj));
  if (!obj)
    ldso_fail("out of memory loading", name);
  obj->name = name; // points into the requestor's strtab: stays mapped
  obj->base = bias;
  obj->span = hi; // covers [bias, bias+hi); see ldso_impl.h
  // PHDRs live at file offset e_phoff, i.e. mapping start + e_phoff (the
  // headers sit in the first LOAD like everywhere else).
  obj->phdr = (uintptr_t)map + ehdr->e_phoff;
  obj->phnum = ehdr->e_phnum;
  obj->loads = loads;
  obj->nloads = nloads;
  obj->next = 0;
  __cavos_parse_dynamic(obj, bias + file_dyn);
  return obj;
}

// Load every DT_NEEDED of one object that is not already in the chain,
// appending to the tail. Returns the (possibly new) tail.
CavosObj *__cavos_load_needed(CavosObj *head, CavosObj *self, CavosObj *obj,
                             uintptr_t dyn, CavosObj *tail, size_t *loaded) {
  for (const Elf64_Dyn *d = (const Elf64_Dyn *)dyn; d->d_tag != DT_NULL; d++) {
    if (d->d_tag != DT_NEEDED)
      continue;
    const char *name = obj->strtab + d->d_un.d_val;
    int         seen = 0;
    for (CavosObj *o = head; o; o = o->next) {
      if (strcmp(o->name, name) == 0) {
        seen = 1;
        break;
      }
    }
    if (seen)
      continue;
    if (*loaded >= LDSO_LIB_MAX)
      ldso_fail("too many NEEDED libraries", name);
    int       is_new = 0;
    CavosObj *lib = __cavos_load_library(head, self, name, &is_new);
    if (!is_new)
      continue; // SONAME already loaded (possibly ourselves): nothing to add
    tail->next = lib;
    tail = lib;
    (*loaded)++;
  }
  return tail;
}

// JMPREL entries are RELA records here; anything else cannot be parsed.
int __cavos_relative_only_check(CavosObj *obj) {
  if (obj->pltrelsz && obj->pltrel != DT_RELA)
    ldso_fail("unsupported PLT relocation format", obj->name);
  return 1;
}

// Predecessor of obj in the exe-anchored chain (for reverse-order init).
// The chain is short; linear search is fine.
CavosObj *__cavos_prev_in_chain(CavosObj *head, CavosObj *obj) {
  for (CavosObj *o = head; o && o->next; o = o->next) {
    if (o->next == obj)
      return o;
  }
  return head;
}

CavosObj *__cavos_dl_head = 0;
CavosObj *__cavos_dl_self = 0;

void __cavos_ldso_boot(uintptr_t base, void *sp) {
  char **argv;
  char **envp;
  Auxv   aux;
  __cavos_parse_stack(sp, &argv, &envp, &aux);
  environ = envp;
  __environ = envp;
  ld_debug = getenv("CAVOS_LD_DEBUG") != 0;
  if (aux.random)
    memcpy(&__stack_chk_guard, aux.random, sizeof(__stack_chk_guard));

  if (!aux.phdr || !aux.entry)
    ldso_fail("no AT_PHDR/AT_ENTRY on stack", 0);

  // File offset 0 of the executable from AT_PHDR (linkers place PHDR
  // right after the EHDR, so e_phoff == sizeof(Ehdr)). A wrong guess fails
  // the magic check loudly.
  uintptr_t exe_file0 = aux.phdr - sizeof(Elf64_Ehdr);
  const Elf64_Ehdr *eehdr = (const Elf64_Ehdr *)exe_file0;
  if (eehdr->e_ident[0] != 0x7f || eehdr->e_ident[1] != 'E' ||
      eehdr->e_ident[2] != 'L' || eehdr->e_ident[3] != 'F')
    ldso_fail("cannot locate executable image", 0);
  const Elf64_Phdr *ehdrs = (const Elf64_Phdr *)aux.phdr;
  uintptr_t         exe_base = exe_file0 - __cavos_first_load_vaddr(ehdrs, aux.phnum);

  // Own bias from our EHDR at the interpreter base.
  const Elf64_Ehdr *sehdr0 = (const Elf64_Ehdr *)base;
  const Elf64_Phdr *sphdrs0 =
      (const Elf64_Phdr *)(base + sehdr0->e_phoff);
  uintptr_t self_bias = base - __cavos_first_load_vaddr(sphdrs0, sehdr0->e_phnum);

  if (exe_base == self_bias) {
    // Direct execution of the library itself: identify, don't recurse.
    ldso_write(1, LDSO_VERSION_LINE);
    __syscall1(SYS_exit_group, 0);
    __builtin_unreachable();
  }

  DBG("ldso: self base=");
  dbg_ulong(base);
  DBG("\n");
  // Self + executable descriptors live on the heap, not the stack: the
  // boot stack becomes the executable's stack after the jump, and dlopen()
  // keeps consulting this chain for the rest of the process. Spans back
  // the containment checks dlsym(RTLD_NEXT) needs.
  CavosObj *self = malloc(sizeof(*self));
  CavosObj *exe = malloc(sizeof(*exe));
  if (!self || !exe)
    ldso_fail("out of memory", 0);
  self->loads = 0;
  self->nloads = 0;
  // Our own PHDRs: libc sources carry no __thread today, so this usually
  // registers nothing, but if one ever appears its offsets stay valid.
  self->phdr = base + sehdr0->e_phoff;
  self->phnum = sehdr0->e_phnum;
  exe->loads = 0;
  exe->nloads = 0;
  self->name = "libc";
  self->base = self_bias;
  self->next = 0;
  self->dyn = 0;
  {
    const Elf64_Ehdr *sehdr = (const Elf64_Ehdr *)base;
    self->dyn = __cavos_find_dynamic(
        (const Elf64_Phdr *)(base + sehdr->e_phoff), sehdr->e_phnum,
        self->base);
    if (!self->dyn)
      ldso_fail("own DYNAMIC not found", 0);
    __cavos_parse_dynamic(self, self->dyn);
    uintptr_t hi = 0;
    const Elf64_Phdr *sph =
        (const Elf64_Phdr *)(base + sehdr->e_phoff);
    for (size_t i = 0; i < sehdr->e_phnum; i++) {
      if (sph[i].p_type != PT_LOAD)
        continue;
      if (sph[i].p_vaddr + sph[i].p_memsz > hi)
        hi = sph[i].p_vaddr + sph[i].p_memsz;
    }
    self->span = hi;
  }

  // Executable descriptor (mapped by the kernel at executableBase).
  exe->name = argv[0] ? argv[0] : "(executable)";
  exe->base = exe_base;
  exe->phdr = aux.phdr;
  exe->phnum = aux.phnum;
  exe->next = 0;
  exe->dyn = __cavos_find_dynamic((const Elf64_Phdr *)aux.phdr, aux.phnum, exe_base);
  if (exe->dyn)
    __cavos_parse_dynamic(exe, exe->dyn);
  {
    // Executable span for containment checks (same LOAD walk as above).
    const Elf64_Phdr *eph = (const Elf64_Phdr *)aux.phdr;
    uintptr_t         hi = 0;
    for (size_t i = 0; i < aux.phnum; i++) {
      if (eph[i].p_type != PT_LOAD)
        continue;
      if (eph[i].p_vaddr + eph[i].p_memsz > hi)
        hi = eph[i].p_vaddr + eph[i].p_memsz;
    }
    exe->span = hi;
  }

  DBG("ldso: exe ");
  DBG(exe->name);
  DBG(" entry=");
  dbg_ulong(aux.entry);
  DBG("\n");

  // Breadth-first NEEDED load: exe first, then each library's own NEEDEDs.
  // Self is deliberately NOT in the chain - nothing NEEDEDs it by load;
  // it joins the bind scope below as the last resort (its symbols are the
  // ABI every other object was built against here).
  size_t    loaded = 0;
  CavosObj *tail = exe;
  for (CavosObj *obj = exe; obj; obj = obj->next) {
    if (!obj->dyn)
      continue;
    tail = __cavos_load_needed(exe, self, obj, obj->dyn, tail, &loaded);
  }
  __cavos_dl_head = exe;
  __cavos_dl_self = self;

  // Bind scope: executable first, then NEEDED breadth-first, our own
  // library last (it defines what everyone else imports).
  size_t    nscope = 0;
  CavosObj *scope[LDSO_LIB_MAX + 2];
  scope[nscope++] = exe;
  for (CavosObj *obj = exe->next; obj; obj = obj->next)
    scope[nscope++] = obj;
  scope[nscope++] = self;

  // Static TLS, exe first then breadth-first NEEDEDs then ourselves: the
  // same order the static linker assigns initial-exec offsets in, so IE
  // accesses agree with this layout. The main thread installs before any
  // INIT/INIT_ARRAY runs or main is reached (%fs:0x28 canary reads).
  for (CavosObj *obj = exe; obj; obj = obj->next) {
    if (obj->phdr && obj->phnum)
      __cavos_tls_register_image((const void *)obj->phdr, obj->phnum,
                                 obj->base);
  }
  if (self->phdr && self->phnum)
    __cavos_tls_register_image((const void *)self->phdr, self->phnum,
                               self->base);
  __cavos_install_main_tcb();

  // Pass 1: RELATIVE (+RELR) everywhere. Pass 2: symbolic everywhere
  // except COPY. Pass 3: COPY everywhere (it reads relocated content).
  // Missing imports are collected across all objects and reported together.
  int missing = 0;
  for (size_t pass = 0; pass < 3; pass++) {
    for (size_t s = 0; s < nscope; s++) {
      if (!scope[s]->dyn)
        continue;
      if (!__cavos_relative_only_check(scope[s]))
        continue;
      DBG("ldso: bind ");
      DBG(scope[s]->name);
      DBG(pass == 0 ? " relative\n" : pass == 1 ? " symbolic\n" : " copy\n");
      if (__cavos_bind_object(scope[s], scope, nscope, pass == 0, pass == 2))
        missing = 1;
      DBG("ldso: bound ");
      DBG(scope[s]->name);
      DBG("\n");
    }
  }
  if (missing)
    ldso_fail("unresolved imports (listed above)", 0);

  // Now that no more GOT writes happen, harden the libraries we mapped:
  // TEMP-DEBUG: protection pass disabled to isolate the init crash.
  // per-segment page-rounded protection. Best-effort per segment; the
  // executable keeps whatever mapping the kernel gave it.
  for (size_t sidx = 0; sidx < nscope; sidx++) {
    CavosObj *obj = scope[sidx];
    for (size_t li = 0; li < obj->nloads; li++) {
      int prot = 0;
      if (obj->loads[li].flags & PF_R)
        prot |= PROT_READ;
      if (obj->loads[li].flags & PF_W)
        prot |= PROT_WRITE;
      if (obj->loads[li].flags & PF_X)
        prot |= PROT_EXEC;
      uintptr_t seg_start = obj->base + obj->loads[li].vaddr;
      uintptr_t seg_end = seg_start + obj->loads[li].memsz;
      uintptr_t page_start = seg_start & ~(uintptr_t)0xfff;
      uintptr_t page_end = (seg_end + 0xfff) & ~(uintptr_t)0xfff;
      __cavos_ret((unsigned long)__syscall3(SYS_mprotect, page_start,
                                           (long)(page_end - page_start),
                                           prot));
    }
  }

  // Library initializers, leaves first (reverse load order, exe excluded -
  // its own _start runs its INIT through __libc_start_main). Array slots
  // hold link-time addresses, rebased like everything else.
  for (CavosObj *obj = tail; obj != exe; obj = __cavos_prev_in_chain(exe, obj)) {
    DBG("ldso: init ");
    DBG(obj->name);
    DBG(" base=");
    dbg_ulong(obj->base);
    DBG(" init=");
    dbg_ulong(obj->init);
    DBG(" array=");
    dbg_ulong(obj->init_array);
    DBG(" arraysz=");
    dbg_ulong(obj->init_arraysz);
    DBG("\n");
    if (obj->init) {
      ((void (*)(void))obj->init)();
      DBG("ldso: inited ");
      DBG(obj->name);
      DBG("\n");
    }
    for (size_t i = 0; i + sizeof(uintptr_t) <= obj->init_arraysz;
         i += sizeof(uintptr_t)) {
      // Array slots already hold runtime addresses: RELATIVE relocations
      // cover .init_array, so the binder converted them during pass 1.
      // Adding the base again double-counts (a real bug caught live here).
      // Slots below the base would be unconverted link values - handle both.
      uintptr_t slot = *(uintptr_t *)(obj->init_array + i);
      uintptr_t fn = slot >= obj->base ? slot : obj->base + slot;
      if (fn != obj->base) {
        ((void (*)(void))fn)();
        DBG("ldso: inited-array ");
        DBG(obj->name);
        DBG("\n");
      }
    }
  }

  DBG("ldso: jump\n");
  __asm__ __volatile__("mov %0, %%rsp\n\txor %%ebp, %%ebp\n\tjmp *%1" ::"r"(sp),
                       "r"(aux.entry)
                       : "memory");
  __builtin_unreachable();
}
