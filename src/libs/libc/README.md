# cavOS libc

Small freestanding C library that builds **both** halves of the cavOS
userspace from one source tree:

| target | artifact    | for                        |
| ------ | ----------- | -------------------------- |
| `mho`  | `libc.dylib`| Mach-O binaries (dynamic)  |
| `elf`  | `libc.so`   | ELF binaries (dynamic)     |

With no arguments (`hmake`, `hmake all`, `hmake compile`) both are built.

```
src/libs/libc/
  Makefile          elf / mho / all / compile / install / clean
  include/          public, musl-compatible headers
    libc.h          compat shim: pulls the standard headers, keeps
                    `#include <libc.h>` working (entry stub, macho-test)
    stdio.h ...     vendored musl-1.2.5 public headers, configured for x86_64
    bits/ sys/ ...  (kept as a tree - never flattened on install)
  src/
    internal/       private headers (NOT installed)
      syscall_arch.h  raw `syscall` stubs, verbatim musl arch/x86_64
      syscall.h       cavOS wrapper layer: SYS_* + errno mapping
      heap.h          brk-heap shared state
      impl.h          musl-private helper decls (see below)
      printf_impl.h   shared format engine interface
      stdio_impl.h    FILE layout (unbuffered)
      dirent_impl.h   DIR layout for getdents64 parsing
      ldso_impl.h     program-interpreter object model
    errno/ ctype/ env/ fcntl/ mman/ malloc/ misc/
    process/ stdio/ stdlib/ string/ sys/ dirent/
    unistd/           one file per function, musl module layout
    macho/            dyld_stub_binder.asm (NASM, Mach-O link only)
    ldso/             program interpreter (NASM entry + C boot, ELF only)
```

## musl compatibility

Anything that compiles against musl headers should compile against
`include/` and link against either artifact:

- **Headers** are musl-1.2.5's, taken from the configured tree
  (`src/libs/musl/cavos-out/include`, i.e. `include/` + `arch/generic` +
  `arch/x86_64` bits + generated `alltypes.h`/`syscall.h`). Signatures are
  therefore musl's: `ssize_t read(...)`, `off_t lseek(...)`, `int brk(void*)`,
  .... To regenerate after a musl bump: rebuild `hmake musl`, then copy
  `src/libs/musl/cavos-out/include/` over `include/` (keeping `libc.h`).
- **Syscall numbers** come from the vendored `<sys/syscall.h>` and match the
  kernel's Linux-compatible table, so the same wrapper sources serve ELF and
  Mach-O objects.
- **String/ctype/stdlib leaves** (`memcpy`, `strlen`, `atoi`, `getenv`, ...)
  are musl-1.2.5 sources, verbatim except for the two adaptations below.
- **Build with `-std=gnu17 -D_GNU_SOURCE`** (see Makefile, and
  `tools/userland/macho-clang` for apps): musl's `<features.h>` only enables
  the default BSD/POSIX API under a non-strict dialect.

Two musl idioms cannot survive the Mach-O half, and are adapted:

1. `weak_alias` (musl's `environ`/`stpcpy`/`memrchr`/... aliases). Mach-O has
   no weak aliases, so public names are plain wrappers in the same file, and
   `environ`/`__environ` are two cells kept in sync by the single writer, the
   entry stub (`src/libs/macho/crt0.c`).
2. `src/internal/*` (thread/cancellation machinery behind every musl
   syscall wrapper). Replaced by `src/internal/syscall.h`: raw `__syscallN`
   plus the `-4095..-1 means -errno` mapping musl's `__syscall_ret` applies.

## Deliberately not vendored

- `mallocng`, `stdio`/`FILE`, `pthread`, `ldso`, locale: they depend on the
  thread implementation and on kernel services cavOS does not provide on the
  Mach-O path. The allocator is a small `brk`-grown first-fit heap
  (`src/malloc/`); `printf` formats into a bounded buffer and writes through;
  `errno` is one global cell.
- The headers still *declare* the full musl API. Referencing an unimplemented
  function fails at link time, not silently - that is intentional.

## Install note

`hmake install` copies both libraries to `target/lib/` and merges the headers
into `target/usr/include/`. The disk flow intentionally does *not* install:
`target/lib` precedes `target/usr/lib` on the ELF `-lc` search path, so an
installed `libc.so` would shadow musl's full libc for ports. Install only for
binaries written against this libc's documented coverage.

## Coverage

errno (thread-local via TCB, global fallback pre-TLS), environ/getenv/setenv/
putenv/unsetenv/clearenv/__libc_start_main, pthreads (create/join/detach/
self/equal/exit, attr, once, keys/TSD with destructors, mutex
normal/recursive/errorcheck, cond, rwlock, spin, barrier, cancel-deferred,
atfork, sched_yield) and C11 threads (thrd/mtx/cnd/tss/call_once) on both
targets, static ELF TLS (__thread in exe + initial libraries, __tls_get_addr
for GD/LD; dlopen of TLS libraries refused loudly), signal (sigaction with
SA_RESTORER trampoline, sigprocmask/sigemptyset/
sigaddset/sigdelset/sigismember/kill/killpg) sufficient for bash job control,
ctype (isspace/isdigit), string (+strdup/strndup/
strchrnul/stpcpy/stpncpy/memrchr/strcspn/strspn/strtok/strerror), stdio
(printf/snprintf/vsnprintf/fprintf/vfprintf/puts/putchar/perror, plus an
unbuffered FILE core: fopen/fclose/fread/fwrite/fgets/fseek/ftell/feof/
ferror/clearerr/fflush/setvbuf/fdopen and stdin/stdout/stderr), stdlib (atoi/abort),
malloc/calloc/realloc/free, sbrk/brk, read/write/close/lseek/unlink/isatty,
open/openat, opendir/readdir/closedir, mmap/munmap, getpid/gettid,
fork/execve/waitpid/wait, setuid/setgid, mount, ioctl, syscall,
exit/_exit/_Exit, plus legacy `syscall_getpid/syscall_gettid`
wrappers and the Mach-O `dyld_stub_binder`.

Build products: `hmake mho` -> libc.dylib, `hmake elf` -> libc.so (both in
`all`); `hmake static` -> libc.a for the -Bstatic links in-tree ELF software
uses. Headers install as a tree, never flattened.

Not implemented (declared by the vendored headers, fail loudly at link):
buffered FILE internals beyond the unbuffered core above, locales,
dlfcn TLS (dlopen of a PT_TLS library fails with an error, musl-style),
robust/PI mutexes (ENOTSUP at init), async cancel (deferred only),
`__thread` on Mach-O (the toolchain cannot compile it for
x86_64-apple-darwin: pthread keys/TSD are the thread-local story there),
timed lock waits past what the kernel's relative-timeout futex offers.

## Program interpreter (src/ldso)

`libc.so` doubles as `/lib/ld-musl-x86_64.so.1`: the kernel jumps to its
`_dlstart` entry for every dynamic exec. The NASM entry self-relocates and
self-binds using `st_value` only (valid: zero undefined symbols), then calls
the C boot, which loads the NEEDED chain (absolute SONAMEs as-is, bare names
via `CAVOS_LD_PATH`, `/lib/`, `/usr/lib/`), applies RELATIVE/RELR then
symbolic relocs (GLOB_DAT/JUMP_SLOT/COPY, linear dynsym scan, SYSV hash),
runs library INIT/INIT_ARRAY, and jumps to the executable entry. Same-SONAME
objects (symlinked musl names included) reuse the already-loaded self.

Unresolvable imports print `cannot resolve SYM (needed by OBJ)` to stderr
and exit 127 - Alpine binaries surface exactly what is still missing.
Deliberate limits, all diagnosed: no TLS/threads, no symbol versioning,
no ifunc, RELRO left writable, `CAVOS_LD_PATH` honored for testing only.
Symbol counts come from the SYSV hash, or from walking GNU bucket chains
(Alpine ships GNU-hash-only) when it is absent.

## Replacing musl (tested procedure)

Static cavOS software links `-Bstatic` by default, so `target/lib/libc.a`
shadows musl's archive with no musl files touched. Full replacement means
musl's headers and binaries go away and the same paths serve ours. Every
step needs sudo; back up the musl files first so it is fully reversible:

    # 0. our headers over musl's (same API, plus libc.h)
    sudo cp -R src/libs/libc/include/. target/usr/include/
    # 1. our binaries in place (static archive must be a copy, the .so too)
    sudo cp src/libs/libc/libc.a target/usr/lib/libc.a
    sudo cp src/libs/libc/libc.so target/lib/libc.so
    # 2. remove the musl binaries, symlink the same paths to ours
    sudo mv target/lib/ld-musl-x86_64.so.1 /tmp/ld-musl-backup
    sudo mv target/lib/libc.musl-x86_64.so.1 /tmp/libc.musl-backup
    sudo rm target/usr/lib/libc.so
    sudo ln -s libc.so target/lib/ld-musl-x86_64.so.1    # Alpine INTERP -> us
    sudo ln -s libc.so target/lib/libc.musl-x86_64.so.1  # Alpine SONAME -> us
    sudo ln -s ../../lib/libc.so target/usr/lib/libc.so # -lc (dynamic) -> us

then rebuild the software loop and bake/boot as usual. What runs after
that: static programs link and run; our own dynamic binaries run through
the interpreter above; foreign (musl-ABI) binaries fail at exec with the
precise missing-symbol list instead of crashing. Restore from the backups
(and `sudo rm target/lib/libc.a` if the shadow was placed) to revert.
