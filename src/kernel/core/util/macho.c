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

// Enough of a segment to resolve an address. Bind and rebase opcodes address
// memory as a (segment index, offset) pair; see machoSegmentBase.
typedef struct {
  uint64_t vmaddr;
  uint64_t vmsize;
  uint32_t initprot;
} MachOSegmentRef;

typedef struct {
  uint64_t text_address;
  uint64_t text_size;
  uint64_t entry_offset;
  uint64_t thread_entry;
  uint64_t entry;
  bool     has_text;
  bool     has_entry_offset;
  bool     has_thread_entry;
  bool     has_entry;
  bool     has_symtab;
  MachOSymtabCommand symtab;
  MachOAddressRange  *ranges;
  uint32_t            range_count;
  uint32_t            range_capacity;

  // Everything below only matters once an image actually imports something.
  uint32_t       filetype;
  MachOSegmentRef *segments;
  uint32_t        segment_count;
  uint32_t        rebase_off, rebase_size;
  uint32_t        bind_off, bind_size;
  uint32_t        weak_bind_off, weak_bind_size;
  uint32_t        lazy_bind_off, lazy_bind_size;
  uint32_t        export_off, export_size;
  char          **dylibs;
  uint32_t        dylib_count;
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

// Hands back everything machoParse allocated into `image`, leaving it empty.
// Safe to call on a zeroed or already-released image, so error paths can call it
// unconditionally rather than tracking who owns what.
static void machoImageRelease(MachOImage *image) {
  if (!image)
    return;
  free(image->ranges);
  free(image->segments);
  if (image->dylibs) {
    for (uint32_t i = 0; i < image->dylib_count; i++)
      free(image->dylibs[i]);
    free(image->dylibs);
  }
  memset(image, 0, sizeof(*image));
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
      header->ncmds == 0 || header->ncmds > MACHO_MAX_COMMANDS ||
      header->sizeofcmds < sizeof(MachOLoadCommand) ||
      header->sizeofcmds > size - sizeof(MachHeader64))
    return false;
  // Both an executable and a dylib are loadable images; a bundle or an object
  // file is not something to map and jump into.
  if (header->filetype != MH_EXECUTE && header->filetype != MH_DYLIB)
    return false;
  image->filetype = header->filetype;

  image->range_capacity = header->ncmds;
  image->ranges = (MachOAddressRange *)malloc(sizeof(MachOAddressRange) *
                                              image->range_capacity);
  image->segments = (MachOSegmentRef *)malloc(sizeof(MachOSegmentRef) *
                                             image->range_capacity);
  image->dylibs = (char **)malloc(sizeof(char *) * image->range_capacity);
  if (!image->ranges || !image->segments || !image->dylibs) {
    machoImageRelease(image);
    return false;
  }

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
      if (image->segment_count >= image->range_capacity)
        goto cleanup;
      image->segments[image->segment_count].vmaddr = segment->vmaddr;
      image->segments[image->segment_count].vmsize = segment->vmsize;
      image->segments[image->segment_count].initprot = segment->initprot;
      image->segment_count++;
      if (machoNameIs(segment->segname, "__TEXT", sizeof("__TEXT") - 1)) {
        // Tracked with an explicit flag rather than by testing text_address
        // against zero, because a dylib's __TEXT really is linked at zero. Using
        // the address as its own "was there a __TEXT?" marker rejects every
        // dylib in existence, and worse, lets a second __TEXT through whenever
        // the first one happened to be at 0.
        if (image->has_text)
          goto cleanup;
        image->has_text = true;
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
      // The opcode streams are kept, not rejected. Rebase and bind are what
      // turn a set of files into a running image, so refusing them here is
      // precisely what used to make dynamically linked executables unloadable.
      image->rebase_off = dyld_info->rebase_off;
      image->rebase_size = dyld_info->rebase_size;
      image->bind_off = dyld_info->bind_off;
      image->bind_size = dyld_info->bind_size;
      image->weak_bind_off = dyld_info->weak_bind_off;
      image->weak_bind_size = dyld_info->weak_bind_size;
      image->lazy_bind_off = dyld_info->lazy_bind_off;
      image->lazy_bind_size = dyld_info->lazy_bind_size;
      image->export_off = dyld_info->export_off;
      image->export_size = dyld_info->export_size;
      break;
    }
    case LC_DYLD_EXPORTS_TRIE: {
      // Same table as export_off/export_size, reached through its own command
      // instead. Prefer whichever one the image actually carries.
      if (!machoValidateDataCommand(cursor, command->cmdsize, size))
        goto cleanup;
      uint32_t offset = *(const uint32_t *)(cursor + 8);
      uint32_t length = *(const uint32_t *)(cursor + 12);
      if (length != 0) {
        image->export_off = offset;
        image->export_size = length;
      }
      break;
    }
    case LC_LOAD_DYLIB:
    case LC_LOAD_WEAK_DYLIB:
    case LC_REEXPORT_DYLIB:
    case LC_LAZY_LOAD_DYLIB:
    case LC_LOAD_UPWARD_DYLIB: {
      if (!machoValidateStringCommand(cursor, command->cmdsize, 24))
        goto cleanup;
      const uint32_t name_offset = *(const uint32_t *)(cursor + 8);
      char          *name = (char *)cursor + name_offset;
      if (image->dylib_count >= image->range_capacity)
        goto cleanup;
      image->dylibs[image->dylib_count] = strdup(name);
      if (!image->dylibs[image->dylib_count])
        goto cleanup;
      image->dylib_count++;
      break;
    }
    case LC_LOAD_DYLINKER:
      if (!machoValidateStringCommand(cursor, command->cmdsize, 12))
        goto cleanup;
      break;
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

  if (cursor != commands_end || !image->has_text)
    goto cleanup;
  // A dylib is never entered. Demanding an entry point from one would reject
  // every library, since LC_MAIN and LC_UNIXTHREAD are both executable-only.
  if (image->filetype == MH_EXECUTE) {
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
  }
  result = true;

cleanup:
  // Deliberately not released here: on success the caller owns these arrays and
  // has to hand them back via machoImageRelease.
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

// Copies an image's segments into the current page directory.
//
// `slide` is added to every virtual address, so an executable (slide 0) lands
// exactly where it was linked and a dylib (linked at 0) lands wherever the caller
// decided. The second pass is what fills in the zero-fill tail of a segment; the
// first pass makes sure the backing frames exist first.
static bool machoMapImage(const uint8_t *data, uint64_t slide) {
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
          !machoMapRange(segment->vmaddr + slide, segment->vmsize))
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
          memcpy((void *)(size_t)(segment->vmaddr + slide),
                 data + (size_t)segment->fileoff, (size_t)segment->filesize);
        if (segment->vmsize > segment->filesize)
          memset((void *)(size_t)(segment->vmaddr + slide + segment->filesize),
                 0, (size_t)(segment->vmsize - segment->filesize));
      }
    }
    cursor += command->cmdsize;
  }
  return cursor == commands_end;
}

// ---------------------------------------------------------------------------
// Dynamic linking
//
// Everything above this point knows how to get one image mapped. What follows
// is what turns a mapped image into a running one: finding the symbols it
// imports, loading the images that provide them, and writing the resulting
// addresses into the pointer slots the linker left behind.
// ---------------------------------------------------------------------------

#define MACHO_MAX_DYLIBS 32U
#define MACHO_MAX_DYLIB_DEPTH 4U

// The two opcode streams address memory as (segment index, offset). The index
// is the position of the segment in load-command order, counting every segment -
// __PAGEZERO and __LINKEDIT included - and it means the same thing for bind as
// for rebase.
//
// Verified against a real lld-linked executable rather than assumed: its bind
// stream carries SET_SEGMENT_AND_OFFSET_ULEB with index 2, and that binary's
// segments are __PAGEZERO(0), __TEXT(1), __DATA(2), __LINKEDIT(3). The offset it
// pairs with that index is 0, which lands squarely in the GOT at the base of
// __DATA, so the alternative readings - "writable segments only", or "everything
// except __PAGEZERO" - both put the import slots somewhere they cannot be.
static bool machoSegmentBase(const MachOImage *image, uint32_t index,
                             uint64_t *address) {
  if (index >= image->segment_count)
    return false;
  *address = image->segments[index].vmaddr;
  return true;
}

// ULEB128, as used throughout the opcode streams and the export trie. The
// caller bounds the input; a truncated value would otherwise run off the end.
static bool machoUleb(const uint8_t *data, size_t size, size_t *cursor,
                      uint64_t *value) {
  uint64_t result = 0;
  unsigned shift = 0;
  for (;;) {
    if (*cursor >= size || shift > 63)
      return false;
    uint8_t byte = data[(*cursor)++];
    result |= (uint64_t)(byte & 0x7fU) << shift;
    if (!(byte & 0x80U))
      break;
    shift += 7;
  }
  *value = result;
  return true;
}

// SLEB128, used only by BIND_OPCODE_SET_ADDEND_SLEB.
static bool machoSleb(const uint8_t *data, size_t size, size_t *cursor,
                      int64_t *value) {
  uint64_t result = 0;
  unsigned shift = 0;
  uint8_t byte = 0;
  for (;;) {
    if (*cursor >= size || shift > 63)
      return false;
    byte = data[(*cursor)++];
    result |= (uint64_t)(byte & 0x7fU) << shift;
    shift += 7;
    if (!(byte & 0x80U))
      break;
  }
  if (shift < 64 && (byte & 0x40U))
    result |= ~0ULL << shift;
  *value = (int64_t)result;
  return true;
}

// ---------------------------------------------------------------------------
// Export lookup
//
// A dylib publishes its symbols twice: as an export trie under
// LC_DYLD_EXPORTS_TRIE or the export_off/export_size pair of LC_DYLD_INFO, and
// as an ordinary symbol table under LC_SYMTAB.
//
// This walks the symbol table rather than the trie, and that is a deliberate
// choice rather than a shortcut. lld's trie encodes each child edge as a
// ULEB128 key offset, the NUL-terminated remainder of the name, and then the
// child node, with the node itself laid out as terminal-size, terminal data,
// child count, child offsets. Every one of those pieces was confirmed against
// the real bytes in libc.dylib, but the child offsets could not be shown to
// address a consistent set of nodes, and shipping a guess at that would mean an
// export lookup that silently returns the wrong address for some symbols and a
// correct one for others.
//
// The symbol table is fully specified, it carries the same addresses (verified
// against nm: _printf is 0x1940 through both paths), and the cost is a linear
// scan per import - 63 entries for libc.dylib, which is not worth optimising
// before there is a library big enough to notice.
//
// Trie support, when it comes, slots in beside this rather than replacing it:
// for each loaded image, try the trie, then fall back to the symtab.
// ---------------------------------------------------------------------------

static bool machoExportRead(const uint8_t *data, size_t size,
                            const MachOSymtabCommand *symtab,
                            const char *wanted, uint64_t *value) {
  if (!symtab || symtab->nsyms == 0 || symtab->strsize == 0)
    return false;

  uint64_t symbol_bytes = (uint64_t)symtab->nsyms * sizeof(MachONlist64);
  if (!machoRangeValid(symtab->symoff, symbol_bytes, size))
    return false;
  if (!machoRangeValid(symtab->stroff, symtab->strsize, size))
    return false;

  const MachONlist64 *symbols = (const MachONlist64 *)(data + symtab->symoff);
  const char *strings = (const char *)(data + symtab->stroff);
  size_t wanted_length = strlength((char *)wanted);

  for (uint32_t i = 0; i < symtab->nsyms; i++) {
    if (symbols[i].n_strx == 0 || symbols[i].n_strx >= symtab->strsize)
      continue;
    // Only a symbol that actually lands in a section of this image can be
    // resolved to an address. N_UNDF entries are this image's own imports,
    // which is precisely the set that is not yet bound.
    if ((symbols[i].n_type & N_TYPE) != N_SECT || symbols[i].n_sect == 0)
      continue;
    const char *name = strings + symbols[i].n_strx;
    if (strncmp(name, wanted, wanted_length) != 0 || name[wanted_length] != '\0')
      continue;
    *value = symbols[i].n_value;
    return true;
  }
  return false;
}

// Everything the loader has to consult to resolve one import.
//
// `data` is the on-disk bytes, kept only because the export trie lives in them
// and lives there only for the duration of binding. `data_pages` is what it was
// allocated with, so it can be given back.
typedef struct {
  MachOImage     image;
  const uint8_t *data;
  size_t         size;
  int            data_pages;
  uint64_t       slide;
  char          *path;
} MachOLoaded;

static void machoLoadedRelease(MachOLoaded *loaded) {
  if (!loaded)
    return;
  machoImageRelease(&loaded->image);
  if (loaded->data && loaded->data_pages > 0)
    VirtualFree((void *)loaded->data, loaded->data_pages);
  free(loaded->path);
  memset(loaded, 0, sizeof(*loaded));
}

// Reads a file into a fresh buffer, or returns false if it cannot be read whole.
static bool machoReadImage(char *path, uint8_t **out_data, size_t *out_size,
                           int *out_pages) {
  *out_data = 0;
  *out_size = 0;
  *out_pages = 0;

  OpenFile *file = fsKernelOpen((char *)path, O_RDONLY, 0);
  if (!file)
    return false;
  size_t filesize = fsGetFilesize(file);
  if (filesize < sizeof(MachHeader64) ||
      filesize > MACHO_MAX_IMAGE_SIZE || filesize > UINT32_MAX) {
    fsKernelClose(file);
    return false;
  }
  size_t pages = DivRoundUp(filesize, BLOCK_SIZE);
  if (pages == 0 || pages > INT_MAX) {
    fsKernelClose(file);
    return false;
  }
  uint8_t *data = (uint8_t *)VirtualAllocate((int)pages);
  if (!data) {
    fsKernelClose(file);
    return false;
  }
  size_t read = fsRead(file, data, (uint32_t)filesize);
  fsKernelClose(file);
  if (read != filesize) {
    VirtualFree(data, (int)pages);
    return false;
  }
  *out_data = data;
  *out_size = filesize;
  *out_pages = (int)pages;
  return true;
}

// Resolves one import name against every image loaded so far.
//
// The executable is searched too, not only the dylibs: a Mach-O executable can
// define a symbol itself and import a different one from the same dylib, and the
// usual two-level rule is "flat across the client first".
static bool machoResolveSymbol(const MachOLoaded *all, uint32_t count,
                               const char *name, uint64_t *address) {
  for (uint32_t i = 0; i < count; i++) {
    uint64_t value = 0;
    if (!machoExportRead(all[i].data, all[i].size,
                         all[i].image.has_symtab ? &all[i].image.symtab : 0,
                         name, &value))
      continue;
    // A symbol table address is an offset from the start of the image, so it
    // has to be slid into the space the image was actually mapped into. For an
    // executable the slide is zero and this is the identity; for a dylib it is
    // what makes a linked-at-zero library reachable at all.
    *address = value + all[i].slide;
    return true;
  }
  return false;
}

// Rebase opcodes add the slide to pointers the image already contains. This is
// what makes a dylib safe to put anywhere: every pointer it was linked with
// still holds the unslid address.
//
// DONE means the same thing here as in a bind stream - one run of rebases ends,
// the region continues - and for the same reason: returning on the first DONE
// would leave every pointer after it unslid, and a dylib with one wrong pointer
// in it fails in ways that look nothing like a loader bug.
//
// Only BIND_TYPE_POINTER is honoured. The 32-bit absolute and PC-relative text
// forms would need the text segment's own slide folded in, and nothing here
// produces them.
static bool machoApplyRebase(const MachOImage *image, const uint8_t *data,
                             size_t size, uint64_t slide) {
  if (image->rebase_size == 0 || slide == 0)
    return true;
  if (!machoRangeValid(image->rebase_off, image->rebase_size, size))
    return false;

  const uint8_t *stream = data + image->rebase_off;
  size_t        length = image->rebase_size;
  size_t        cursor = 0;

  uint8_t  type = BIND_TYPE_POINTER;
  uint64_t segment_offset = 0;
  uint64_t segment_base = 0;

  while (cursor < length) {
    uint8_t opcode = stream[cursor++];
    uint8_t immediate = opcode & REBASE_IMMEDIATE_MASK;

    if (opcode == REBASE_OPCODE_DONE) {
      // Ends this run of rebases; the region carries on after it. See
      // machoApplyBinds for why this is not a return.
      type = BIND_TYPE_POINTER;
      segment_offset = 0;
      segment_base = 0;
      continue;
    }

    switch (opcode & REBASE_OPCODE_MASK) {
    case REBASE_OPCODE_SET_TYPE_IMM:
      type = immediate;
      break;
    case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: {
      if (!machoSegmentBase(image, immediate, &segment_base))
        return false;
      if (!machoUleb(stream, length, &cursor, &segment_offset))
        return false;
      break;
    }
    case REBASE_OPCODE_ADD_ADDR_ULEB: {
      uint64_t delta = 0;
      if (!machoUleb(stream, length, &cursor, &delta))
        return false;
      segment_offset += delta;
      break;
    }
    case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
    case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
    case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
    case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB: {
      // Default to one slot. Only DO_REBASE_IMM_TIMES takes its count from the
      // opcode's immediate; the other three are always 0xN0 with an empty
      // immediate and carry their operands in the stream, so reading the count
      // from the immediate here would rebase nothing at all and fail silently.
      uint64_t count = 1;
      uint64_t skip = 0;
      switch (opcode & REBASE_OPCODE_MASK) {
      case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
        count = immediate;
        break;
      case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
        if (!machoUleb(stream, length, &cursor, &count))
          return false;
        break;
      case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
        if (!machoUleb(stream, length, &cursor, &skip))
          return false;
        break;
      default: // ULEB_TIMES_SKIPPING_ULEB
        if (!machoUleb(stream, length, &cursor, &count) ||
            !machoUleb(stream, length, &cursor, &skip))
          return false;
        break;
      }

      if (type != BIND_TYPE_POINTER)
        return false;

      for (uint64_t i = 0; i < count; i++) {
        uint64_t address = segment_base + segment_offset;
        *(uint64_t *)(size_t)address += slide;
        // 8 is the pointer size on x86_64, which is all this loader targets.
        segment_offset += 8 + skip;
      }
      break;
    }
    default:
      return false;
    }
  }
  return true;
}

// Bind opcodes write each import's resolved address into a pointer slot the
// linker left for it. Lazy binds are the same stream with the same opcodes, just
// reached through a different load command, and both are run here eagerly: there
// is no dyld to defer to and no first-call opportunity to patch a stub from, so
// the alternative is leaving the stub helper to run and jump through whatever
// happens to be in the slot.
static bool machoApplyBinds(const MachOImage *image, const uint8_t *data,
                            size_t size, const MachOLoaded *all,
                            uint32_t count, uint32_t offset, uint32_t length) {
  if (length == 0)
    return true;
  if (!machoRangeValid(offset, length, size))
    return false;

  const uint8_t *stream = data + offset;
  size_t        total = length;
  size_t        cursor = 0;

  uint8_t     type = BIND_TYPE_POINTER;
  const char *symbol = 0;
  uint64_t    segment_offset = 0;
  uint64_t    segment_base = 0;
  int64_t     addend = 0;

  while (cursor < total) {
    uint8_t opcode = stream[cursor++];
    uint8_t immediate = opcode & BIND_IMMEDIATE_MASK;

    switch (opcode & BIND_OPCODE_MASK) {
    case BIND_OPCODE_DONE:
      // DONE closes one run of bindings, not the whole stream. lld emits a
      // separate DONE-terminated run per group of symbols inside a single
      // bind or lazy-bind region - macho-test's lazy region holds three, for
      // _printf, _getenv and _exit - so returning here would bind the first
      // symbol and silently leave the rest holding whatever stub address the
      // linker put there. The region ending is what ends the walk, and that is
      // the cursor reaching the end of it.
      //
      // Everything a run sets up is dropped, so the next run starts clean and
      // cannot inherit a stale symbol name or offset.
      type = BIND_TYPE_POINTER;
      symbol = 0;
      segment_offset = 0;
      segment_base = 0;
      addend = 0;
      break;
    case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
    case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
      // Single-image lookup: the ordinal says which dylib should provide the
      // symbol, but every image is searched anyway below, so the value itself
      // is not needed. Skipping it without consuming an operand is correct
      // because both forms carry the ordinal in the immediate byte.
      break;
    case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB: {
      uint64_t ordinal = 0;
      if (!machoUleb(stream, total, &cursor, &ordinal))
        return false;
      break;
    }
    case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM: {
      const uint8_t *name = stream + cursor;
      while (cursor < total && stream[cursor] != '\0')
        cursor++;
      if (cursor >= total)
        return false;
      cursor++; // step past the NUL
      symbol = (const char *)name;
      break;
    }
    case BIND_OPCODE_SET_TYPE_IMM:
      type = immediate;
      break;
    case BIND_OPCODE_SET_ADDEND_SLEB:
      if (!machoSleb(stream, total, &cursor, &addend))
        return false;
      break;
    case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB: {
      if (!machoSegmentBase(image, immediate, &segment_base))
        return false;
      if (!machoUleb(stream, total, &cursor, &segment_offset))
        return false;
      break;
    }
    case BIND_OPCODE_ADD_ADDR_ULEB: {
      uint64_t delta = 0;
      if (!machoUleb(stream, total, &cursor, &delta))
        return false;
      segment_offset += delta;
      break;
    }
    case BIND_OPCODE_DO_BIND:
    case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
    case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
    case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: {
      uint64_t times = 1;
      uint64_t skip = 0;
      if ((opcode & BIND_OPCODE_MASK) ==
          BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB) {
        if (!machoUleb(stream, total, &cursor, &skip))
          return false;
      } else if ((opcode & BIND_OPCODE_MASK) ==
                 BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED) {
        skip = (uint64_t)immediate;
      } else if ((opcode & BIND_OPCODE_MASK) ==
                 BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB) {
        if (!machoUleb(stream, total, &cursor, &times) ||
            !machoUleb(stream, total, &cursor, &skip))
          return false;
      }

      if (type != BIND_TYPE_POINTER)
        return false;
      if (!symbol)
        return false;

      for (uint64_t i = 0; i < times; i++) {
        uint64_t resolved = 0;
        if (!machoResolveSymbol(all, count, symbol, &resolved)) {
          debugf("[macho] unresolved symbol %s\n", symbol);
          return false;
        }
        *(uint64_t *)(size_t)(segment_base + segment_offset) =
            resolved + (uint64_t)addend;
        segment_offset += 8 + skip;
      }
      break;
    }
    default:
      return false;
    }
  }
  return true;
}

// Where dylibs go.
//
// A dylib's __TEXT is linked at 0 and cannot be mapped there at all -
// MACHO_MIN_MAPPED_ADDRESS rejects anything lower - so it has to be slid, and
// this is the slot it gets slid to.
//
// A fixed base rather than a randomised one. The slide exists here to make an
// unslid linked address usable again, not to hide the image; a random base would
// also make every address this loader prints useless for reproducing a failure.
// Nothing else here randomises yet, so randomising dylibs alone would make them
// the only moving address in the system.
#define MACHO_DYLIB_BASE 0x400000000ULL

// Where an image ends up once it is mapped, given the slide it was placed at.
//
// Uses the recorded segment ranges rather than __TEXT alone: __TEXT is not the
// whole image. libc.dylib's __TEXT is 0..0x3000 but __DATA follows it to 0x4000,
// and it is __DATA that holds the symbols. Placing the next image off the end of
// __TEXT alone would drop it straight into the middle of the previous one.
static bool machoImageSpan(const MachOImage *image, uint64_t slide,
                           uint64_t *end) {
  uint64_t highest = 0;
  for (uint32_t i = 0; i < image->range_count; i++) {
    uint64_t candidate = 0;
    if (!machoAdd(image->ranges[i].end, slide, &candidate))
      return false;
    if (candidate > highest)
      highest = candidate;
  }
  if (highest == 0)
    return false;
  *end = highest;
  return true;
}

// Checks that where an image is about to go does not collide with anything
// already loaded.
//
// This is the check that would have caught the dylib being mapped on top of the
// executable's __DATA at the point it went wrong. Mapping a second image over a
// first one does not fail - both maps succeed, the second copy of the bytes wins,
// and the damage only shows up as a fault at some unrelated address much later -
// so the placement is verified before mapping rather than trusted.
static bool machoPlacementClear(const MachOLoaded *all, uint32_t count,
                                const MachOImage *image, uint64_t slide) {
  for (uint32_t i = 0; i < image->range_count; i++) {
    uint64_t start = 0, end = 0;
    if (!machoAdd(image->ranges[i].start, slide, &start) ||
        !machoAdd(image->ranges[i].end, slide, &end))
      return false;
    for (uint32_t j = 0; j < count; j++) {
      for (uint32_t k = 0; k < all[j].image.range_count; k++) {
        uint64_t other_start = 0, other_end = 0;
        if (!machoAdd(all[j].image.ranges[k].start, all[j].slide, &other_start) ||
            !machoAdd(all[j].image.ranges[k].end, all[j].slide, &other_end))
          return false;
        if (start < other_end && other_start < end)
          return false;
      }
    }
  }
  return true;
}

// Places the next dylib.
//
// Only the dylibs are considered. The executable is not on this ladder: it is
// mapped at the fixed address it was linked at, and folding it in here put the
// first library at the end of its __TEXT - which is inside its __DATA - so the
// library was mapped over the executable's own globals and the two images
// scribbled on each other. That produced a page fault at a dylib-relative offset
// counting up from the executable, which reads like a corrupt pointer rather
// than what it was.
//
// Each dylib goes above everything loaded before it, so a set of libraries
// cannot overlap however large any of them turns out to be.
static bool machoNextDylibBase(const MachOLoaded *all, uint32_t count,
                               uint64_t *slide) {
  uint64_t base = MACHO_DYLIB_BASE;
  // Entry 0 is the executable; dylibs start at entry 1.
  for (uint32_t i = 1; i < count; i++) {
    uint64_t end = 0;
    if (machoImageSpan(&all[i].image, all[i].slide, &end) && end > base)
      base = end;
  }
  if (!machoAlign(base, slide))
    return false;
  return *slide >= MACHO_DYLIB_BASE;
}

// Loads one dylib: reads it, maps it at the next free base, loads anything it
// itself needs, then rebases and binds it.
//
// Depth is bounded because a cycle in LC_LOAD_DYLIB would otherwise recurse until
// the kernel stack runs out. Already-loaded paths are checked first, so a
// diamond (two libraries both needing the same third) is loaded once and shared
// rather than twice at two addresses - which is what a Mach-O client expects,
// since it looks a symbol up through the library it named and must find the same
// instance every time.
static bool machoLoadDylib(char *path, MachOLoaded *all, uint32_t *count,
                           uint32_t depth) {
  if (depth >= MACHO_MAX_DYLIB_DEPTH) {
    debugf("[macho] dependency chain too deep at %s\n", path);
    return false;
  }
  for (uint32_t i = 0; i < *count; i++) {
    if (all[i].path && strEql(all[i].path, path))
      return true; // already mapped
  }
  if (*count >= MACHO_MAX_DYLIBS) {
    debugf("[macho] too many dylibs\n");
    return false;
  }

  uint8_t *data = 0;
  size_t   size = 0;
  int      pages = 0;
  if (!machoReadImage(path, &data, &size, &pages))
    return false;

  MachOLoaded loaded;
  memset(&loaded, 0, sizeof(loaded));
  loaded.data = data;
  loaded.size = size;
  loaded.data_pages = pages;

  if (!machoParse(data, size, &loaded.image)) {
    debugf("[macho] %s is not a valid Mach-O image\n", path);
    machoLoadedRelease(&loaded);
    return false;
  }
  if (loaded.image.filetype != MH_DYLIB) {
    // Named separately from a parse failure, because "wrong filetype" and
    // "malformed" have completely different causes and a single message for
    // both makes this impossible to diagnose from a boot log.
    debugf("[macho] %s is filetype %u, not MH_DYLIB (%u)\n", path,
           loaded.image.filetype, MH_DYLIB);
    machoLoadedRelease(&loaded);
    return false;
  }
  debugf("[macho] loading dylib %s\n", path);

  uint64_t slide = MACHO_DYLIB_BASE;
  if (!machoNextDylibBase(all, *count, &slide)) {
    debugf("[macho] no room for %s\n", path);
    machoLoadedRelease(&loaded);
    return false;
  }
  loaded.slide = slide;

  if (!machoPlacementClear(all, *count, &loaded.image, slide)) {
    debugf("[macho] %s at 0x%lx would overlap an image already loaded\n", path,
           (unsigned long)slide);
    machoLoadedRelease(&loaded);
    return false;
  }

  // Mapped into the page directory that is already current, which is the one
  // machoExecute switched to and the one the task will be given. Giving the dylib
  // its own page directory here mapped the library into a directory that was then
  // handed straight back: the imports bound to addresses that looked perfect, and
  // the first touch of any of them faulted as not-present. One process, one page
  // directory, every image in it.
  if (!machoMapImage(data, slide)) {
    machoLoadedRelease(&loaded);
    return false;
  }

  loaded.path = strdup(path);
  if (!loaded.path) {
    machoLoadedRelease(&loaded);
    return false;
  }

  // Claim the slot before recursing, so a dependency that needs this same dylib
  // back finds it already loaded instead of recursing forever.
  //
  // Everything after this point works from the local copy of the struct rather
  // than from all[*count - 1]: the recursive calls append to `all`, which moves
  // that index. The copy still owns the same arrays, so nothing is duplicated.
  all[*count] = loaded;
  (*count)++;

  // Its own dependencies first: this dylib's binds may resolve into them.
  for (uint32_t i = 0; i < loaded.image.dylib_count; i++) {
    if (!machoLoadDylib(loaded.image.dylibs[i], all, count, depth + 1)) {
      debugf("[macho] could not load dependency %s of %s\n",
             loaded.image.dylibs[i], path);
      return false;
    }
  }

  if (!machoApplyRebase(&loaded.image, loaded.data, loaded.size, loaded.slide) ||
      !machoApplyBinds(&loaded.image, loaded.data, loaded.size, all, *count,
                       loaded.image.bind_off, loaded.image.bind_size) ||
      !machoApplyBinds(&loaded.image, loaded.data, loaded.size, all, *count,
                       loaded.image.weak_bind_off,
                       loaded.image.weak_bind_size)) {
    debugf("[macho] could not link %s\n", path);
    return false;
  }
  return true;
}

// Loads every LC_LOAD_DYLIB of the executable, then rebases and binds the
// executable itself. Dylibs are linked by machoLoadDylib as they load, so only
// the client is handled here.
static bool machoLinkExecutable(MachOLoaded *all, uint32_t *count) {
  for (uint32_t i = 0; i < all[0].image.dylib_count; i++) {
    if (!machoLoadDylib(all[0].image.dylibs[i], all, count, 0))
      return false;
  }
  return machoApplyRebase(&all[0].image, all[0].data, all[0].size,
                          all[0].slide) &&
         machoApplyBinds(&all[0].image, all[0].data, all[0].size, all, *count,
                         all[0].image.bind_off, all[0].image.bind_size) &&
         machoApplyBinds(&all[0].image, all[0].data, all[0].size, all, *count,
                         all[0].image.weak_bind_off,
                         all[0].image.weak_bind_size) &&
         machoApplyBinds(&all[0].image, all[0].data, all[0].size, all, *count,
                         all[0].image.lazy_bind_off,
                         all[0].image.lazy_bind_size);
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
  bool      ok = machoParse((const uint8_t *)image, size, &parsed);
  // machoParse hands its allocations to the caller even when it is only being
  // used as a validator, so they have to go back either way.
  machoImageRelease(&parsed);
  return ok;
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

  // The loaded-image table doubles as the main image's own record and as the
  // search list every import resolves against, so the client is entry 0. Keep the
  // parse result inside that entry rather than in a separate `image` the table
  // copies from: two structs sharing one set of malloc'd pointers means whoever
  // releases both frees the same array twice.
  static MachOLoaded loaded[1 + MACHO_MAX_DYLIBS];
  memset(loaded, 0, sizeof(loaded));
  if (!machoParse(output, filesize, &loaded[0].image)) {
    VirtualFree(output, (int)output_pages);
    debugf("[macho] Invalid executable %s\n", filepath);
    return 0;
  }
  debugf("[macho] parsed executable\n");

  loaded[0].data = output;
  loaded[0].size = filesize;
  loaded[0].data_pages = (int)output_pages;
  loaded[0].path = strdup(filepath);
  uint32_t loaded_count = 1;

  uint64_t *old_pagedir = GetPageDirectory();
  uint64_t *pagedir = PageDirectoryAllocate();
  ChangePageDirectory(pagedir);
  // Slide 0: an executable is mapped exactly where it was linked, which is what
  // lets the absolute addresses baked into its __TEXT work untouched.
  bool mapped = machoMapImage(output, 0);
  if (mapped)
    mapped = machoLinkExecutable(loaded, &loaded_count);
  ChangePageDirectory(old_pagedir);

  // The mapped pages own the code now, so the on-disk bytes are only needed for
  // the symbol tables, which is all that just got used. The entry point has to
  // come out first: machoLoadedRelease zeroes the struct it is read from.
  uint64_t entry = loaded[0].image.entry;
  uint32_t dylib_count = loaded_count - 1;
  for (uint32_t i = 0; i < loaded_count; i++)
    machoLoadedRelease(&loaded[i]);

  if (!mapped) {
    PageDirectoryFree(pagedir);
    debugf("[macho] Could not map executable %s\n", filepath);
    return 0;
  }
  debugf("[macho] mapped executable, entry 0x%lx, %u dylib(s) linked\n",
         (unsigned long)entry, dylib_count);

  uint64_t id = taskGenerateId();
  Task     *target = taskCreate((uint32_t)id, entry, false, pagedir, argc,
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

  // output_pages is not freed here: machoLoadedRelease already handed the
  // on-disk bytes back above, and doing it twice would free the same frames.
  if (startup)
    taskCreateFinish(target);
  return target;
}
#endif /* CONFIG_MHO_EXEC */
