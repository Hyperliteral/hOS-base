#include <bitmap.h>
#include <console.h>
#include <elf.h>
#include <fb.h>
#include <malloc.h>
#include <paging.h>
#include <pmm.h>
#include <stack.h>
#include <nic_controller.h>
#include <socket.h>
#include <string.h>
#include <syscalls.h>
#include <system.h>
#include <task.h>
#include <timer.h>
#include <util.h>
#include <vfs.h>
#include <vmm.h>

// ELF (for now only 64) parser
// Copyright (C) 2024 Panagiotis

#define ELF_DEBUG 0

#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

// ELF user executable support. The relocatable-object half of this file (the
// .ko kernel module loader, further down) is always compiled in - it is an
// internal kernel facility, not an executable format the userspace can pick.
#if CONFIG_EXEC_ELF

bool elf_check_file(Elf64_Ehdr *hdr) {
  if (!hdr)
    return false;
  if (hdr->e_ident[EI_MAG0] != ELFMAG0) {
    debugf("[elf] Header EI_MAG0 incorrect.\n");
    return false;
  }
  if (hdr->e_ident[EI_MAG1] != ELFMAG1) {
    debugf("[elf] Header EI_MAG1 incorrect.\n");
    return false;
  }
  if (hdr->e_ident[EI_MAG2] != ELFMAG2) {
    debugf("[elf] Header EI_MAG2 incorrect.\n");
    return false;
  }
  if (hdr->e_ident[EI_MAG3] != ELFMAG3) {
    debugf("[elf] Header EI_MAG3 incorrect.\n");
    return false;
  }
  if (hdr->e_ident[EI_CLASS] != ELFCLASS64 ||
      hdr->e_machine != ELF_x86_64_MACHINE) {
    debugf("[elf] Architecture is not supported.\n");
    return false;
  }
  return true;
}

void elfProcessLoad(Elf64_Phdr *elf_phdr, uint8_t *out, size_t base) {
  // Map the (current) program page
  size_t   startRounded = (elf_phdr->p_vaddr & ~0xFFF);
  uint64_t pagesRequired = DivRoundUp(
      (elf_phdr->p_vaddr - startRounded) + elf_phdr->p_memsz, 0x1000);
  for (int j = 0; j < pagesRequired; j++) {
    size_t vaddr = (elf_phdr->p_vaddr & ~0xFFF) + j * 0x1000;
    if (VirtualToPhysical(base + vaddr))
      continue;
    size_t paddr = PhysicalAllocate(1);
    VirtualMap(base + vaddr, paddr, PF_USER | PF_RW);
  }

  // Copy the required info
  memcpy((void *)(base + elf_phdr->p_vaddr), out + elf_phdr->p_offset,
         elf_phdr->p_filesz);

  // wtf is this? (needed)
  if (elf_phdr->p_memsz > elf_phdr->p_filesz)
    memset((void *)(base + elf_phdr->p_vaddr + elf_phdr->p_filesz), 0,
           elf_phdr->p_memsz - elf_phdr->p_filesz);
}

Task *elfExecute(char *filepath, uint32_t argc, char **argv, uint32_t envc,
                 char **envv, bool startup) {
  debugf("opening...");

  // Open & read executable file
  OpenFile *dir = fsKernelOpen(filepath, O_RDONLY, 0);
  if (!dir) {
    debugf("[elf] Could not open %s\n", filepath);
    return 0;
  }
  size_t filesize = fsGetFilesize(dir);
#if ELF_DEBUG
  debugf("[elf] Executing %s: filesize{%d}\n", filepath, filesize);
#endif
  size_t    outSize = DivRoundUp(filesize, BLOCK_SIZE);
  uint8_t *out = (uint8_t *)VirtualAllocate(outSize);
  fsRead(dir, out, filesize);
  fsKernelClose(dir);

  debugf("casting header..");

  // Cast ELF32 header
  Elf64_Ehdr *elf_ehdr = (Elf64_Ehdr *)(out);

  if (!elf_check_file(elf_ehdr)) {
    debugf("[elf] File %s is not a valid ELF executable!\n", filepath);
    return 0;
  }

  // Create a new page directory which is later used by the process
  uint64_t *oldpagedir = GetPageDirectory();
  uint64_t *pagedir = PageDirectoryAllocate();
  ChangePageDirectory(pagedir);

#if ELF_DEBUG
  debugf("\n[elf_ehdr] entry=%x type=%d arch=%d\n", elf_ehdr->e_entry,
         elf_ehdr->e_type, elf_ehdr->e_machine);
  debugf("[early phdr] offset=%x count=%d size=%d\n", elf_ehdr->e_phoff,
         elf_ehdr->e_phnum, elf_ehdr->e_phentsize);
#endif

  int32_t id = taskGenerateId();
  if (id == -1) {
    debugf(
        "[elf] Cannot fetch task id... You probably reached the task limit!");
    // optional
    // printf("[elf] Cannot fetch task id... You probably reached the task
    // limit!");
    panic();
  }

  size_t interpreterEntry = 0;
  size_t interpreterBase = 0x100000000000; // todo: not hardcode
  size_t executableBase = 0;               // modified later if needed
  // Loop through the multiple ELF32 program header tables
  for (int i = 0; i < elf_ehdr->e_phnum; i++) {
    Elf64_Phdr *elf_phdr = (Elf64_Phdr *)((size_t)out + elf_ehdr->e_phoff +
                                          i * elf_ehdr->e_phentsize);
    if (elf_phdr->p_type == 3) {
      char     *interpreterFilename = (char *)(out + elf_phdr->p_offset);
      OpenFile *interpreter = fsKernelOpen(interpreterFilename, O_RDONLY, 0);
      if (!interpreter) {
        debugf("[elf] Interpreter path{%s} could not be found!\n",
               interpreterFilename);
        panic();
      }
      size_t size = fsGetFilesize(interpreter);

      size_t    intContentsSize = DivRoundUp(size, BLOCK_SIZE);
      uint8_t *interpreterContents =
          (uint8_t *)VirtualAllocate(intContentsSize);
      fsRead(interpreter, interpreterContents, size);
      fsKernelClose(interpreter);

      Elf64_Ehdr *interpreterEhdr = (Elf64_Ehdr *)(interpreterContents);
      if (interpreterEhdr->e_type != 3) { // ET_DYN
        debugf("[elf::dyn] Interpreter{%s} isn't really of type ET_DYN!\n",
               interpreterFilename);
        panic();
      }
      interpreterEntry = interpreterEhdr->e_entry;
      for (int i = 0; i < interpreterEhdr->e_phnum; i++) {
        Elf64_Phdr *interpreterPhdr =
            (Elf64_Phdr *)((size_t)interpreterContents +
                           interpreterEhdr->e_phoff +
                           i * interpreterEhdr->e_phentsize);
        if (interpreterPhdr->p_type != PT_LOAD)
          continue;
        elfProcessLoad(interpreterPhdr, interpreterContents, interpreterBase);
      }
      VirtualFree(interpreterContents, intContentsSize);

      continue;
    }
    if (elf_phdr->p_type != PT_LOAD)
      continue;

    if (elf_ehdr->e_type == 3) {
      // ET_DYN
      executableBase = 0x50000000000; // todo: not hardcode
    }
    elfProcessLoad(elf_phdr, out, executableBase);

#if ELF_DEBUG
    debugf("[elf] Program header: type{%d} offset{%x} vaddr{%x} size{%x} "
           "alignment{%x}\n",
           elf_phdr->p_type, elf_phdr->p_offset, elf_phdr->p_vaddr,
           elf_phdr->p_memsz, elf_phdr->p_align);
#endif
  }

  // For the foreseeable future ;)
#if ELF_DEBUG
  // for (int i = 0; i < elf_ehdr->e_shnum; i++) {
  //   Elf64_Shdr *elf_shdr = (Elf64_Shdr *)((uint32_t)out + elf_ehdr->e_shoff +
  //                                         i * elf_ehdr->e_shentsize);
  //   debugf("[elf] Section header: type{%d} offset{%lx}\n", elf_shdr->sh_type,
  //          elf_shdr->sh_offset);
  // }
#endif

#if ELF_DEBUG
  debugf("[elf] New pagedir: offset{%x}\n", pagedir);
#endif

  // Done loading into the pagedir
  ChangePageDirectory(oldpagedir);

  debugf("\n creating task");

  Task *target =
      taskCreate(id,
                 interpreterEntry ? (interpreterBase + interpreterEntry)
                                  : (executableBase + elf_ehdr->e_entry),
                 false, pagedir, argc, argv);

  size_t totalLen = 0;
  for (int curr = 0; curr < argc; curr++)
    totalLen += strlength(argv[curr]) + 1;
  char  *cmdline = malloc(totalLen);
  size_t strCurr = 0;
  for (int curr = 0; curr < argc; curr++) {
    int f = strlength(argv[curr]) + 1;
    memcpy(&cmdline[strCurr], argv[curr], f);
    strCurr += f;
  }

  debugf("stackgenerateuser task");

  target->cmdline = cmdline;
  target->cmdlineLen = totalLen;
  target->execname = strdup(filepath);

  // User stack generation: the stack itself, AUXs, etc...
  stackGenerateUser(target, argc, argv, envc, envv, out, filesize, elf_ehdr,
                    interpreterEntry ? 0x100000000000 : 0, executableBase);
  VirtualFree(out, outSize);

  // Align it, just in case...
  spinlockAcquire(&target->infoPd->LOCK_PD);
  taskAdjustHeap(target, DivRoundUp(target->infoPd->heap_end, 0x1000) * 0x1000,
                 &target->infoPd->heap_start, &target->infoPd->heap_end);
  spinlockRelease(&target->infoPd->LOCK_PD);

  // Just a sane default
  target->parent = currentTask;

  if (startup) {
    debugf("finishing");
    taskCreateFinish(target);
  }

  return target;
}

#endif /* CONFIG_EXEC_ELF */

typedef int (*module_init_t)(void);

bool loadKernelModule(char *filepath) {
  debugf("[module] loading %s\n", filepath);
  OpenFile *file = fsKernelOpen(filepath, O_RDONLY, 0);
  if (!file) {
    debugf("[module] Could not open %s\n", filepath);
    return false;
  }

  size_t   filesize = fsGetFilesize(file);
  uint8_t *buff = malloc(filesize);
  fsRead(file, buff, filesize);
  fsKernelClose(file);

  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buff;
  if (ehdr->e_type != ET_REL) {
    debugf("[module] Error: %s is not an ET_REL object\n", filepath);
    free(buff);
    return false;
  }

  Elf64_Shdr *shdr = (Elf64_Shdr *)(buff + ehdr->e_shoff);

  // 1. Allocate sections in high-half kernel address space (near
  // ffffffff82000000)
  static size_t next_module_vaddr = 0xffffffff82000000;

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdr[i].sh_flags & 0x2) { // SHF_ALLOC
      size_t align = shdr[i].sh_addralign ? shdr[i].sh_addralign : 1;
      next_module_vaddr = ALIGN_UP(next_module_vaddr, align);

      size_t size = shdr[i].sh_size;
      shdr[i].sh_addr = next_module_vaddr;

      size_t pages = DivRoundUp(size, 0x1000);
      for (size_t p = 0; p < pages; p++) {
        size_t page_vaddr = next_module_vaddr + (p * 0x1000);
        if (!VirtualToPhysical(page_vaddr)) {
          size_t paddr = PhysicalAllocate(1);
          VirtualMap(page_vaddr, paddr, PF_RW); // Kernel-space RW
        }
      }

      if (shdr[i].sh_type == SHT_PROGBITS) {
        memcpy((void *)shdr[i].sh_addr, buff + shdr[i].sh_offset, size);
      } else if (shdr[i].sh_type == SHT_NOBITS) {
        memset((void *)shdr[i].sh_addr, 0, size);
      }

      next_module_vaddr += pages * 0x1000;
    }
  }

  // 2. Find Symbol Table and String Table
  Elf64_Sym *symtab = 0;
  char      *strtab = 0;
  int        num_symbols = 0;

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdr[i].sh_type == SHT_SYMTAB) {
      symtab = (Elf64_Sym *)(buff + shdr[i].sh_offset);
      num_symbols = shdr[i].sh_size / sizeof(Elf64_Sym);
      strtab = (char *)(buff + shdr[shdr[i].sh_link].sh_offset);
      break;
    }
  }

  if (!symtab || !strtab) {
    debugf("[module] Error: Missing symbol table in %s\n", filepath);
    free(buff);
    return false;
  }

  for (int i = 0; i < num_symbols; i++) {
    if (symtab[i].st_shndx == 0) {
      const char *sym_name = strtab + symtab[i].st_name;
      if (sym_name[0] == '\0')
        continue;

      void *resolved = kernelSymbolFind(sym_name);
      if (!resolved) {
        debugf("[module] Unresolved external symbol: %s\n", sym_name);
        free(buff);
        return false;
      }
      symtab[i].st_value = (Elf64_Addr)resolved;
    } else if (symtab[i].st_shndx < 0xFF00) {
      Elf64_Shdr *target_sec = &shdr[symtab[i].st_shndx];
      symtab[i].st_value += target_sec->sh_addr;
    }
  }

  // Allocate a trampoline buffer for PC-relative relocations.
  // MUST live within the same top-2GB region as the module code,
  // or PC32/PLT32 relocations can't reach it with a 32-bit displacement.
  size_t max_relas = 0;
  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdr[i].sh_type == SHT_RELA) {
      max_relas += shdr[i].sh_size / sizeof(Elf64_Rela);
    }
  }
  size_t tramp_buf_size = ALIGN_UP(max_relas * 16 + 16, 16);

  next_module_vaddr = ALIGN_UP(next_module_vaddr, 16);
  uint8_t *tramp_buf = (uint8_t *)next_module_vaddr;
  size_t   tramp_pages = DivRoundUp(tramp_buf_size, 0x1000);
  for (size_t p = 0; p < tramp_pages; p++) {
    size_t page_vaddr = next_module_vaddr + (p * 0x1000);
    if (!VirtualToPhysical(page_vaddr)) {
      size_t paddr = PhysicalAllocate(1);
      VirtualMap(page_vaddr, paddr, PF_RW);
    }
  }
  next_module_vaddr += tramp_pages * 0x1000;

  uint8_t *tramp_curr = tramp_buf;

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdr[i].sh_type == SHT_RELA) {
      Elf64_Rela *relas = (Elf64_Rela *)(buff + shdr[i].sh_offset);
      int         num_relas = shdr[i].sh_size / sizeof(Elf64_Rela);
      Elf64_Shdr *target_sec = &shdr[shdr[i].sh_info];

      for (int j = 0; j < num_relas; j++) {
        Elf64_Rela *rel = &relas[j];
        uint8_t *target_ptr = (uint8_t *)(target_sec->sh_addr + rel->r_offset);
        uint32_t sym_idx = ELF64_R_SYM(rel->r_info);
        uint32_t rel_type = ELF64_R_TYPE(rel->r_info);
        Elf64_Addr sym_addr = symtab[sym_idx].st_value;

        switch (rel_type) {
        case R_X86_64_PC32:
        case 4: // R_X86_64_PLT32
        {
          tramp_curr[0] = 0xFF;
          tramp_curr[1] = 0x25;
          tramp_curr[2] = 0x00;
          tramp_curr[3] = 0x00;
          tramp_curr[4] = 0x00;
          tramp_curr[5] = 0x00;
          *(uint64_t *)(tramp_curr + 6) = sym_addr;

          Elf64_Addr tramp_addr = (Elf64_Addr)tramp_curr;
          tramp_curr += 16;

          *(uint32_t *)target_ptr =
              (uint32_t)(tramp_addr + rel->r_addend - (Elf64_Addr)target_ptr);

          debugf("[module] trampoline: sym_addr=%lx tramp_addr=%lx "
                 "target_ptr=%lx addend=%ld written=%x\n",
                 sym_addr, tramp_addr, (Elf64_Addr)target_ptr, rel->r_addend,
                 *(uint32_t *)target_ptr);
        } break;
        case R_X86_64_64: // 1, absolute 64-bit data pointer
          *(uint64_t *)target_ptr = (uint64_t)(sym_addr + rel->r_addend);
          break;
        case 10: // R_X86_64_32
          *(uint32_t *)target_ptr = (uint32_t)(sym_addr + rel->r_addend);
          break;
        case 11: // R_X86_64_32S   <-- add this case
          *(int32_t *)target_ptr = (int32_t)(sym_addr + rel->r_addend);
          break;
        default:
          debugf("[module] Warning: Unsupported relocation type %d\n",
                 rel_type);
          break;
        }
      }
    }
  }

  for (int i = 0; i < num_symbols; i++) {
    const char *sym_name = strtab + symtab[i].st_name;
    if (strEql((char *)sym_name, (char *)"init_module")) {
      module_init_t init_func = (module_init_t)symtab[i].st_value;
      debugf("[module] init_module resolved at %lx, tramp_buf=%lx-%lx\n",
             (Elf64_Addr)init_func, (Elf64_Addr)tramp_buf,
             (Elf64_Addr)tramp_buf + tramp_buf_size);
      int ret = init_func();
      debugf("[module] Loaded successfully, init returned %d\n", ret);
      break;
    }
  }

  free(buff);
  return true;
}

/*
void loadModulesFromDirectory(const char *dirpath) {
  debugf("[module] Scanning directory: %s\n", dirpath);
  OpenFile *dir = fsKernelOpen((char *)dirpath, O_RDONLY, 0);
  if (!dir) {
    debugf("[module] Could not open directory: %s\n", dirpath);
    return;
  }
  debugf("[module] Directory opened successfully.\n");

  VFS_Dirent entry;
  while (fsRead(dir, (uint8_t *)&entry, sizeof(VFS_Dirent)) > 0) {
    if (entry.name[0] == '\0') break;
    debugf("[module] Found filesystem entry: %s\n", entry.name);

    size_t len = strlength(entry.name);
    if (len > 3 && strEql((char *)&entry.name[len - 3], (char *)".ko")) {
      char fullpath[512];
      snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry.name);
      debugf("[module] Matching .ko found: %s. Loading...\n", fullpath);
      loadKernelModule("/boot/module.ko");
    }
  }

  fsKernelClose(dir);
}
  */

void loadModulesFromDirectory(const char *dirpath) {
  // Directory scanning disabled; load explicit modules instead.
  debugf("[module] Directory scanning skipped for %s\n", dirpath);
}

typedef struct {
  const char *name;
  void       *addr;
} KernelSymbol;

#define EXPORT_SYMBOL(sym) {#sym, &sym}

static KernelSymbol kernel_symbols[] = {
    EXPORT_SYMBOL(debugf),
    EXPORT_SYMBOL(malloc),
    EXPORT_SYMBOL(free),
    EXPORT_SYMBOL(calloc),
    EXPORT_SYMBOL(memcpy),
    EXPORT_SYMBOL(memset),
    EXPORT_SYMBOL(strEql),
    EXPORT_SYMBOL(fsKernelOpen),
    EXPORT_SYMBOL(fsRead),
    EXPORT_SYMBOL(fsKernelClose),
    EXPORT_SYMBOL(fsUserOpen),
    EXPORT_SYMBOL(fsUserClose),
    EXPORT_SYMBOL(fsUserGetNode),
    EXPORT_SYMBOL(spinlockAcquire),
    EXPORT_SYMBOL(spinlockRelease),
    EXPORT_SYMBOL(handControl),

    // loadable-module networking API:
    EXPORT_SYMBOL(netSinkRegister),
    EXPORT_SYMBOL(netSinkUnregister),
    EXPORT_SYMBOL(netDefaultNIC),
    EXPORT_SYMBOL(netDefaultNICMac),
    EXPORT_SYMBOL(netSendRawMac),
    EXPORT_SYMBOL(netIPv6EnableAll),
    EXPORT_SYMBOL(packetSockProviderRegister),

    // current task (used by module socket providers to create fds)
    EXPORT_SYMBOL(currentTask),

    // kernel module API, so modules can load their own dependencies
    EXPORT_SYMBOL(loadKernelModule),
    EXPORT_SYMBOL(kernelSymbolFind),
    EXPORT_SYMBOL(kernelSymbolRegister),

    {0, 0}};

// Subsystems (linuxkpi, drm, ...) register their exported symbols at runtime
#define MAX_DYNAMIC_SYMBOLS 512
static KernelSymbol dynamic_symbols[MAX_DYNAMIC_SYMBOLS];
static int          dynamic_symbol_count = 0;

void kernelSymbolRegister(const char *name, void *addr) {
  if (dynamic_symbol_count >= MAX_DYNAMIC_SYMBOLS) {
    debugf("[module] FATAL! Dynamic symbol table is full, dropping %s\n", name);
    return;
  }
  dynamic_symbols[dynamic_symbol_count].name = name;
  dynamic_symbols[dynamic_symbol_count].addr = addr;
  dynamic_symbol_count++;
}

void *kernelSymbolFind(const char *name) {
  for (int i = 0; kernel_symbols[i].name; i++) {
    if (strEql((char *)kernel_symbols[i].name, (char *)name)) {
      return kernel_symbols[i].addr;
    }
  }
  for (int i = 0; i < dynamic_symbol_count; i++) {
    if (strEql((char *)dynamic_symbols[i].name, (char *)name)) {
      return dynamic_symbols[i].addr;
    }
  }
  return 0;
}

void loadKernelModules(void) {
  debugf("[module] Scanning /boot for kernel modules...\n");

  OpenFile *dir = fsKernelOpen("/boot", O_RDONLY, 0);
  if (!dir) {
    debugf("[module] Could not open /boot directory\n");
    return;
  }

  struct vfsDirent dirent;
  // Replace fsReadDir with fsRead, checking that we read a full struct
  while (fsRead(dir, (uint8_t *)&dirent, sizeof(struct vfsDirent)) ==
         sizeof(struct vfsDirent)) {
    // Skip empty or deleted entries if your VFS uses a specific marker (e.g.,
    // inode 0)
    if (dirent.d_name[0] == '\0')
      continue;

    size_t len = strlen(dirent.d_name);

    // Check if the file extension is ".ko"
    if (len > 3 && strEql(dirent.d_name + len - 3, (char *)".ko")) {
      char filepath[256];

      // Construct the full path: /boot/<filename>
      if (dirent.d_name[0] == '/') {
        snprintf(filepath, sizeof(filepath), "/boot%s", dirent.d_name);
      } else {
        snprintf(filepath, sizeof(filepath), "/boot/%s", dirent.d_name);
      }

      loadKernelModule(filepath);
    }
  }

  fsKernelClose(dir);
}