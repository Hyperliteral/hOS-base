#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <elf.h>

// __environ is musl-private (no public header declares it); ours is
// declared through the internal header - see src/internal/impl.h.
#include "internal/impl.h"
#include "internal/ldso_impl.h"
#include "internal/pthread_impl.h"

// Process startup handoff for ELF binaries, musl-compatible signature.
//
// The system entry stub (src/libs/system/crt0.c, installed as crt1.o) calls
// this exactly the way musl's own crt1 does. Unlike the old single-threaded
// startup, this installs the main thread's TCB (%fs base) and static TLS
// image first: errno, the stack canary and every __thread variable hang off
// %fs, so nothing may run before it - not init, not main. The executable's
// PHDRs come from auxv (AT_PHDR/AT_PHNUM), parsed the same way the program
// interpreter does in src/ldso/boot.c.
//
// Never returns; ends in _exit() through exit(). Not called on the Mach-O
// path (that entry stub calls main directly and exit() itself).
typedef int (*main_fn)(int, char **, char **);

int __libc_start_main(main_fn main, int argc, char **argv, void (*init)(void),
                      void (*fini)(void), void (*ldso_dummy)(void)) {
  (void)ldso_dummy;
  char **envp = argv + argc + 1;
  environ = envp;
  __environ = envp;
  // Auxv sits past the envp NULL. The kernel always provides AT_PHDR for
  // ELF executions (see stackGenerateUser); without it there is no TLS
  // template to register and the main thread installs bare.
  uintptr_t at_phdr = 0;
  size_t    at_phnum = 0;
  char    **p = envp;
  while (*p)
    p++;
  for (uintptr_t *a = (uintptr_t *)(p + 1); a[0] != 0; a += 2) {
    if (a[0] == 3)
      at_phdr = a[1]; // AT_PHDR
    else if (a[0] == 5)
      at_phnum = a[1]; // AT_PHNUM
  }
  if (at_phdr && at_phnum) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)(at_phdr - sizeof(Elf64_Ehdr));
    const Elf64_Phdr *phdr = (const Elf64_Phdr *)at_phdr;
    uintptr_t         bias = 0;
    for (size_t i = 0; i < at_phnum; i++) {
      if (phdr[i].p_type == PT_LOAD) {
        bias = (uintptr_t)ehdr - phdr[i].p_vaddr;
        break;
      }
    }
    if (ehdr->e_ident[0] == 0x7f && ehdr->e_ident[1] == 'E')
      __cavos_init_main_tls(phdr, at_phnum, bias);
    else
      __cavos_setup_tls();
  } else {
    __cavos_setup_tls();
  }
  if (getenv("CAVOS_LD_DEBUG")) {
    char m[128];
    int n = snprintf(m, sizeof(m), "ldso: start_main argc=%d argv0=%s\n",
                     argc, argv && argv[0] ? argv[0] : "(null)");
    (void)n;
    write(2, m, strlen(m));
  }
  if (init)
    init();
  if (getenv("CAVOS_LD_DEBUG"))
    write(2, "ldso: init done, calling main\n", 29);
  int status = main(argc, argv, envp);
  if (fini)
    fini();
  exit(status);
}
