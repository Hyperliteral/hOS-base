#include <macho.h>
#include <malloc.h>
#include <paging.h>
#include <pmm.h>
#include <stack.h>
#include <string.h>
#include <task.h>
#include <util.h>
#include <vfs.h>
#include <vmm.h>

// The Mach-O loader. Everything here exists only to map and start a Mach-O
// image, so the whole file drops out when the format is not configured in.
#if CONFIG_MHO_EXEC

#define MACHO_PAGE_SIZE 0x1000ULL
#define MACHO_MIN_MAPPED_ADDRESS 0x100000000ULL
#define MACHO_MAX_USER_ADDRESS USER_MMAP_START
#define MACHO_MAX_IMAGE_SIZE (256ULL * 1024ULL * 1024ULL)
#define MACHO_MAX_ARGUMENT_BYTES (64ULL * 1024ULL * 1024ULL)
#define MACHO_MAX_COMMANDS 4096U
#define MACHO_MAX_SYMBOLS (1U << 20)

typedef struct {
  uint64_t start;
  uint64_t end;
} MachOAddressRange;

typedef struct {
  uint64_t text_address;
  uint64_t text_size;
  uint64_t entry_offset;
  uint64_t thread_entry;
  uint64_t entry;
  bool     has_entry_offset;
  bool     has_thread_entry;
  bool     has_entry;
  bool     has_symtab;
  MachOSymtabCommand symtab;
  MachOAddressRange  *ranges;
  uint32_t            range_count;
  uint32_t            range_capacity;
} MachOImage;

static bool machoRangeValid(uint64_t offset, uint64_t length, size_t total) {
  return offset <= (uint64_t)total &&
         length <= (uint64_t)total - offset;
}

static bool machoAdd(uint64_t left, uint64_t right, uint64_t *result) {
  if (left > UINT64_MAX - right)
    return false;
  *result = left + right;
  return true;
}

static bool machoAlign(uint64_t value, uint64_t *result) {
  if (value > UINT64_MAX - (MACHO_PAGE_SIZE - 1))
    return false;
  *result = (value + MACHO_PAGE_SIZE - 1) & ~(MACHO_PAGE_SIZE - 1);
  return true;
}

static bool machoNameIs(const char *name, const char *expected, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (name[i] != expected[i])
      return false;
  }
  return name[length] == '\0';
}

static bool machoRangeOverlaps(uint64_t first_start, uint64_t first_end,
                               uint64_t second_start, uint64_t second_end) {
  return first_start < second_end && second_start < first_end;
}

static bool machoStackArgumentsValid(uint32_t argc, char **argv, uint32_t envc,
                                     char **envv) {
  if ((argc != 0 && !argv) || (envc != 0 && !envv))
    return false;
  uint64_t words = (uint64_t)argc + (uint64_t)envc + 3;
  uint64_t capacity = (uint64_t)USER_STACK_PAGES * PAGE_SIZE / sizeof(uint64_t);
  if (words + 2 > capacity)
    return false;
  size_t bytes = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (!argv[i])
      return false;
    size_t length = strlength(argv[i]);
    if (length >= MACHO_MAX_ARGUMENT_BYTES ||
        bytes > MACHO_MAX_ARGUMENT_BYTES - length - 1)
      return false;
    bytes += length + 1;
  }
  for (uint32_t i = 0; i < envc; i++) {
    if (!envv[i])
      return false;
    size_t length = strlength(envv[i]);
    if (length >= MACHO_MAX_ARGUMENT_BYTES ||
        bytes > MACHO_MAX_ARGUMENT_BYTES - length - 1)
      return false;
    bytes += length + 1;
  }
  return true;
}

static bool machoValidateSection(const MachOSection64 *section, size_t size) {
  uint64_t file_size = section->size;
  MachOSectionType type = (MachOSectionType)(section->flags & 0xff);
  if (type == S_ZEROFILL || type == S_GB_ZEROFILL ||
      type == S_THREAD_LOCAL_ZEROFILL)
    file_size = 0;
  if (!machoRangeValid(section->offset, file_size, size))
    return false;
  uint64_t section_end;
  return machoAdd(section->addr, section->size, &section_end);
}

static bool machoValidateSegment(const MachOSegment64 *segment,
                                 uint32_t command_size, size_t size) {
  if (command_size < sizeof(MachOSegment64))
    return false;
  if (segment->filesize > segment->vmsize)
    return false;
  if (!machoRangeValid(segment->fileoff, segment->filesize, size))
    return false;
  uint64_t segment_end;
  if (!machoAdd(segment->vmaddr, segment->vmsize, &segment_end))
    return false;

  uint64_t section_bytes = (uint64_t)segment->nsects * sizeof(MachOSection64);
  if (section_bytes > command_size - sizeof(MachOSegment64))
    return false;

  const MachOSection64 *sections =
      (const MachOSection64 *)((const uint8_t *)segment +
                               sizeof(MachOSegment64));
  for (uint32_t i = 0; i < segment->nsects; i++) {
    if (!machoValidateSection(&sections[i], size))
      return false;
  }
  return true;
}

static bool machoValidateStringCommand(const uint8_t *command,
                                       uint32_t command_size,
                                       uint32_t minimum_offset) {
  if (command_size < minimum_offset)
    return false;
  uint32_t string_offset = *(const uint32_t *)(command + 8);
  if (string_offset < minimum_offset || string_offset >= command_size)
    return false;
  for (uint32_t i = string_offset; i < command_size; i++) {
    if (command[i] == '\0')
      return true;
  }
  return false;
}

static bool machoValidateDataCommand(const uint8_t *command,
                                     uint32_t command_size, size_t size) {
  if (command_size < 16)
    return false;
  uint32_t offset = *(const uint32_t *)(command + 8);
  uint32_t length = *(const uint32_t *)(command + 12);
  return machoRangeValid(offset, length, size);
}

static bool machoRecordRange(MachOImage *image, uint64_t start, uint64_t end) {
  if (end <= start)
    return true;
  if (image->range_count >= image->range_capacity)
    return false;
  uint64_t first_start = start & ~(MACHO_PAGE_SIZE - 1);
  uint64_t first_end = 0;
  if (!machoAlign(end, &first_end))
    return false;
  for (uint32_t i = 0; i < image->range_count; i++) {
    uint64_t other_start = image->ranges[i].start & ~(MACHO_PAGE_SIZE - 1);
    uint64_t other_end = 0;
    if (!machoAlign(image->ranges[i].end, &other_end))
      return false;
    if (machoRangeOverlaps(first_start, first_end, other_start, other_end))
      return false;
  }
  image->ranges[image->range_count++] = (MachOAddressRange){start, end};
  return true;
}

static bool machoFindSymbolEntry(const MachOImage *image, const uint8_t *data,
                                 size_t size, const char *wanted,
                                 uint64_t *entry) {
  if (!image->has_symtab)
    return false;
  uint64_t symbol_bytes = (uint64_t)image->symtab.nsyms * sizeof(MachONlist64);
  if (!machoRangeValid(image->symtab.symoff, symbol_bytes, size))
    return false;
  if (!machoRangeValid(image->symtab.stroff, image->symtab.strsize, size))
    return false;
  if (image->symtab.strsize == 0)
    return false;

  const MachONlist64 *symbols =
      (const MachONlist64 *)(data + image->symtab.symoff);
  const char *strings = (const char *)(data + image->symtab.stroff);
  size_t wanted_length = 0;
  while (wanted[wanted_length])
    wanted_length++;

  for (uint32_t i = 0; i < image->symtab.nsyms; i++) {
    if (symbols[i].n_strx >= image->symtab.strsize ||
        (symbols[i].n_type & N_TYPE) != N_SECT || symbols[i].n_value == 0)
      continue;
    const char *name = strings + symbols[i].n_strx;
    size_t remaining = image->symtab.strsize - symbols[i].n_strx;
    if (remaining <= wanted_length || name[wanted_length] != '\0')
      continue;
    bool matches = true;
    for (size_t j = 0; j < wanted_length; j++) {
      if (name[j] != wanted[j]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      *entry = symbols[i].n_value;
      return true;
    }
  }
  return false;
}

static bool machoParse(const uint8_t *data, size_t size, MachOImage *image) {
  bool result = false;
  memset(image, 0, sizeof(*image));
  if (!data || size < sizeof(MachHeader64))
    return false;

  const MachHeader64 *header = (const MachHeader64 *)data;
  debugf("[macho] header check\n");
  if (header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_X86_64 ||
      (header->cpusubtype & ~CPU_SUBTYPE_MASK) != CPU_SUBTYPE_X86_64_ALL ||
      header->filetype != MH_EXECUTE || header->ncmds == 0 ||
      header->ncmds > MACHO_MAX_COMMANDS ||
      header->sizeofcmds < sizeof(MachOLoadCommand) ||
      header->sizeofcmds > size - sizeof(MachHeader64))
    return false;

  image->range_capacity = header->ncmds;
  image->ranges = (MachOAddressRange *)malloc(sizeof(MachOAddressRange) *
                                              image->range_capacity);
  if (!image->ranges)
    return false;

  const uint8_t *commands = data + sizeof(MachHeader64);
  const uint8_t *commands_end = commands + header->sizeofcmds;
  const uint8_t *cursor = commands;
  uint64_t text_end = 0;

  for (uint32_t command_index = 0; command_index < header->ncmds;
       command_index++) {
    debugf("[macho] command %u\n", command_index);
    if ((size_t)(commands_end - cursor) < sizeof(MachOLoadCommand))
      goto cleanup;
    const MachOLoadCommand *command = (const MachOLoadCommand *)cursor;
    if (command->cmdsize < sizeof(MachOLoadCommand) ||
        command->cmdsize > commands_end - cursor ||
        (command->cmdsize & (sizeof(MachOLoadCommand) - 1)) != 0)
      goto cleanup;

    switch (command->cmd) {
    case LC_SEGMENT_64: {
      const MachOSegment64 *segment = (const MachOSegment64 *)cursor;
      if (!machoValidateSegment(segment, command->cmdsize, size))
        goto cleanup;
      if (machoNameIs(segment->segname, "__TEXT", sizeof("__TEXT") - 1)) {
        if (image->text_address != 0)
          goto cleanup;
        image->text_address = segment->vmaddr;
        image->text_size = segment->vmsize;
        if (!machoAdd(image->text_address, image->text_size, &text_end))
          goto cleanup;
      }
      if (!machoNameIs(segment->segname, "__PAGEZERO",
                       sizeof("__PAGEZERO") - 1) &&
          !machoNameIs(segment->segname, "__LINKEDIT",
                       sizeof("__LINKEDIT") - 1) &&
          !machoRecordRange(image, segment->vmaddr, segment->vmaddr +
                                                        segment->vmsize))
        goto cleanup;
      break;
    }
    case LC_MAIN: {
      if (command->cmdsize < sizeof(MachOMainCommand) ||
          image->has_entry_offset)
        goto cleanup;
      const MachOMainCommand *main_command =
          (const MachOMainCommand *)cursor;
      image->entry_offset = main_command->entryoff;
      image->has_entry_offset = true;
      break;
    }
    case LC_UNIXTHREAD: {
      if (command->cmdsize < sizeof(MachOThreadCommand))
        goto cleanup;
      const MachOThreadCommand *thread =
          (const MachOThreadCommand *)cursor;
      if (thread->flavor != 4 || thread->count < 21 ||
          command->cmdsize < sizeof(MachOThreadCommand) +
                                  thread->count * sizeof(uint64_t))
        goto cleanup;
      const uint64_t *state =
          (const uint64_t *)(cursor + sizeof(MachOThreadCommand));
      image->thread_entry = state[16];
      image->has_thread_entry = true;
      break;
    }
    case LC_SYMTAB: {
      if (command->cmdsize < sizeof(MachOSymtabCommand) || image->has_symtab)
        goto cleanup;
      const MachOSymtabCommand *symtab =
          (const MachOSymtabCommand *)cursor;
      if (symtab->nsyms > MACHO_MAX_SYMBOLS)
        goto cleanup;
      uint64_t symbol_bytes = (uint64_t)symtab->nsyms * sizeof(MachONlist64);
      if (!machoRangeValid(symtab->symoff, symbol_bytes, size) ||
          !machoRangeValid(symtab->stroff, symtab->strsize, size))
        goto cleanup;
      image->symtab = *symtab;
      image->has_symtab = true;
      break;
    }
    case LC_DYLD_INFO:
    case LC_DYLD_INFO_ONLY: {
      if (command->cmdsize < sizeof(MachODyldInfoCommand))
        goto cleanup;
      const MachODyldInfoCommand *dyld_info =
          (const MachODyldInfoCommand *)cursor;
      if (!machoRangeValid(dyld_info->rebase_off, dyld_info->rebase_size,
                           size) ||
          !machoRangeValid(dyld_info->bind_off, dyld_info->bind_size, size) ||
          !machoRangeValid(dyld_info->weak_bind_off,
                           dyld_info->weak_bind_size, size) ||
          !machoRangeValid(dyld_info->lazy_bind_off,
                           dyld_info->lazy_bind_size, size) ||
          !machoRangeValid(dyld_info->export_off, dyld_info->export_size,
                           size))
        goto cleanup;
      if (dyld_info->rebase_size != 0 || dyld_info->bind_size != 0 ||
          dyld_info->weak_bind_size != 0 || dyld_info->lazy_bind_size != 0)
        goto cleanup;
      break;
    }
    case LC_LOAD_DYLIB:
    case LC_LOAD_WEAK_DYLIB:
    case LC_REEXPORT_DYLIB:
    case LC_LAZY_LOAD_DYLIB:
    case LC_LOAD_UPWARD_DYLIB:
      if (!machoValidateStringCommand(cursor, command->cmdsize, 24))
        goto cleanup;
      goto cleanup;
    case LC_LOAD_DYLINKER:
      if (!machoValidateStringCommand(cursor, command->cmdsize, 12))
        goto cleanup;
      break;
    case LC_DYLD_EXPORTS_TRIE:
    case LC_SEGMENT_SPLIT_INFO:
      if (!machoValidateDataCommand(cursor, command->cmdsize, size))
        goto cleanup;
      break;
    case LC_DYLD_CHAINED_FIXUPS: {
      if (command->cmdsize < 16)
        goto cleanup;
      uint32_t data_size = *(const uint32_t *)(cursor + 12);
      if (data_size != 0 ||
          !machoRangeValid(*(const uint32_t *)(cursor + 8), data_size, size))
        goto cleanup;
      break;
    }
    default:
      break;
    }
    cursor += command->cmdsize;
  }

  if (cursor != commands_end || image->text_address == 0)
    goto cleanup;
  if (image->has_entry_offset) {
    if (!machoAdd(image->text_address, image->entry_offset, &image->entry))
      goto cleanup;
  } else if (image->has_thread_entry) {
    image->entry = image->thread_entry;
  } else {
    image->entry = 0;
    if (!machoFindSymbolEntry(image, data, size, "__start", &image->entry) &&
        !machoFindSymbolEntry(image, data, size, "_main", &image->entry) &&
        !machoFindSymbolEntry(image, data, size, "start", &image->entry))
      goto cleanup;
  }
  if (image->entry < image->text_address || image->entry >= text_end)
    goto cleanup;
  image->has_entry = true;
  result = true;

cleanup:
  free(image->ranges);
  image->ranges = 0;
  return result;
}

static bool machoMapRange(uint64_t address, uint64_t length) {
  if (length == 0)
    return true;
  uint64_t end;
  uint64_t start = address & ~(MACHO_PAGE_SIZE - 1);
  if (!machoAdd(address, length, &end) || end > MACHO_MAX_USER_ADDRESS ||
      start < MACHO_MIN_MAPPED_ADDRESS || start >= end)
    return false;
  if (!machoAlign(end, &end))
    return false;
  for (uint64_t page = start; page < end; page += MACHO_PAGE_SIZE) {
    if (page >= MACHO_MAX_USER_ADDRESS)
      return false;
    if (VirtualToPhysical((size_t)page))
      continue;
    uint64_t physical = PhysicalAllocate(1);
    if (!physical)
      return false;
    VirtualMap(page, physical, PF_USER | PF_RW);
    memset((void *)page, 0, MACHO_PAGE_SIZE);
  }
  return true;
}

static bool machoMapImage(const uint8_t *data) {
  const MachHeader64 *header = (const MachHeader64 *)data;
  const uint8_t *commands = data + sizeof(MachHeader64);
  const uint8_t *commands_end = commands + header->sizeofcmds;
  const uint8_t *cursor = commands;

  for (uint32_t command_index = 0; command_index < header->ncmds;
       command_index++) {
    const MachOLoadCommand *command = (const MachOLoadCommand *)cursor;
    if (command->cmd == LC_SEGMENT_64) {
      const MachOSegment64 *segment = (const MachOSegment64 *)cursor;
      if (!machoNameIs(segment->segname, "__PAGEZERO",
                       sizeof("__PAGEZERO") - 1) &&
          !machoNameIs(segment->segname, "__LINKEDIT",
                       sizeof("__LINKEDIT") - 1) &&
          !machoMapRange(segment->vmaddr, segment->vmsize))
        return false;
    }
    cursor += command->cmdsize;
  }
  if (cursor != commands_end)
    return false;

  cursor = commands;
  for (uint32_t command_index = 0; command_index < header->ncmds;
       command_index++) {
    const MachOLoadCommand *command = (const MachOLoadCommand *)cursor;
    if (command->cmd == LC_SEGMENT_64) {
      const MachOSegment64 *segment = (const MachOSegment64 *)cursor;
      if (!machoNameIs(segment->segname, "__PAGEZERO",
                       sizeof("__PAGEZERO") - 1) &&
          !machoNameIs(segment->segname, "__LINKEDIT",
                       sizeof("__LINKEDIT") - 1)) {
        if (segment->filesize != 0)
          memcpy((void *)(size_t)segment->vmaddr,
                 data + (size_t)segment->fileoff, (size_t)segment->filesize);
        if (segment->vmsize > segment->filesize)
          memset((void *)(size_t)(segment->vmaddr + segment->filesize), 0,
                 (size_t)(segment->vmsize - segment->filesize));
      }
    }
    cursor += command->cmdsize;
  }
  return cursor == commands_end;
}

bool macho_is_header(const void *image, size_t size) {
  if (size < sizeof(uint32_t))
    return false;
  const uint8_t *bytes = (const uint8_t *)image;
  return bytes[0] == 0xcf && bytes[1] == 0xfa && bytes[2] == 0xed &&
         bytes[3] == 0xfe;
}

bool macho_check_file(const void *image, size_t size) {
  MachOImage parsed;
  return machoParse((const uint8_t *)image, size, &parsed);
}

Task *machoExecute(char *filepath, uint32_t argc, char **argv, uint32_t envc,
                    char **envv, bool startup) {
  if (!machoStackArgumentsValid(argc, argv, envc, envv))
    return 0;
  OpenFile *file = fsKernelOpen(filepath, O_RDONLY, 0);
  if (!file)
    return 0;
  size_t filesize = fsGetFilesize(file);
  if (filesize < sizeof(MachHeader64) || filesize > MACHO_MAX_IMAGE_SIZE ||
      filesize > UINT32_MAX) {
    fsKernelClose(file);
    return 0;
  }
  size_t output_pages = DivRoundUp(filesize, BLOCK_SIZE);
  if (output_pages == 0 || output_pages > INT_MAX) {
    fsKernelClose(file);
    return 0;
  }
  uint8_t *output = (uint8_t *)VirtualAllocate((int)output_pages);
  size_t    read = fsRead(file, output, (uint32_t)filesize);
  fsKernelClose(file);
  if (read != filesize) {
    VirtualFree(output, (int)output_pages);
    return 0;
  }

  MachOImage image;
  if (!machoParse(output, filesize, &image)) {
    VirtualFree(output, (int)output_pages);
    debugf("[macho] Invalid executable %s\n", filepath);
    return 0;
  }
  debugf("[macho] parsed executable\n");

  uint64_t *old_pagedir = GetPageDirectory();
  uint64_t *pagedir = PageDirectoryAllocate();
  ChangePageDirectory(pagedir);
  if (!machoMapImage(output)) {
    ChangePageDirectory(old_pagedir);
    PageDirectoryFree(pagedir);
    VirtualFree(output, (int)output_pages);
    debugf("[macho] Could not map executable %s\n", filepath);
    return 0;
  }
  debugf("[macho] mapped executable\n");
  ChangePageDirectory(old_pagedir);

  uint64_t id = taskGenerateId();
  Task     *target = taskCreate((uint32_t)id, image.entry, false, pagedir, argc,
                                argv);
  size_t total_length = 0;
  if (argv) {
    for (uint32_t i = 0; i < argc; i++)
      total_length += strlength(argv[i]) + 1;
  }
  target->cmdline = (char *)malloc(total_length ? total_length : 1);
  target->cmdlineLen = total_length;
  size_t current = 0;
  if (argv) {
    for (uint32_t i = 0; i < argc; i++) {
      size_t length = strlength(argv[i]) + 1;
      memcpy(target->cmdline + current, argv[i], length);
      current += length;
    }
  }
  target->execname = strdup(filepath);
  debugf("[macho] creating stack\n");
  stackGenerateMachO(target, argc, argv, envc, envv);
  debugf("[macho] stack created\n");
  target->parent = currentTask;

  spinlockAcquire(&target->infoPd->LOCK_PD);
  taskAdjustHeap(target, DivRoundUp(target->infoPd->heap_end, PAGE_SIZE) *
                           PAGE_SIZE,
                 &target->infoPd->heap_start, &target->infoPd->heap_end);
  spinlockRelease(&target->infoPd->LOCK_PD);
  debugf("[macho] task prepared\n");

  VirtualFree(output, (int)output_pages);
  if (startup)
    taskCreateFinish(target);
  return target;
}
#endif /* CONFIG_MHO_EXEC */
