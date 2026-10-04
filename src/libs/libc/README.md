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
    errno/ ctype/ env/ fcntl/ mman/ malloc/
    process/ stdio/ stdlib/ string/
    unistd/           one file per function, musl module layout
    macho/            dyld_stub_binder.S (Mach-O link only)
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

errno, environ/getenv, ctype (isspace/isdigit), string (+strdup/strndup/
strchrnul/stpcpy/stpncpy/memrchr/strcspn/strspn/strtok), stdio
(printf/puts/putchar), stdlib (atoi/abort), malloc/calloc/realloc/free,
sbrk/brk, read/write/close/lseek/unlink/isatty, open/openat, mmap/munmap,
getpid/gettid, exit/_exit/_Exit, plus legacy `syscall_getpid/syscall_gettid`
wrappers and the Mach-O `dyld_stub_binder`.
