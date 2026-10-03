#ifndef MACHO_H
#define MACHO_H

#include "task.h"
#include "types.h"

#define MH_MAGIC_64 0xfeedfacfU
#define MH_CIGAM_64 0xcffaedfeU
#define CPU_TYPE_X86_64 0x01000007U
#define CPU_SUBTYPE_X86_64_ALL 0x00000003U
#define CPU_SUBTYPE_MASK 0xff000000U

#define MH_OBJECT 0x1U
#define MH_EXECUTE 0x2U
#define MH_DYLIB 0x6U
#define MH_BUNDLE 0x8U
#define MH_DYLDLINK 0x4U
#define MH_PIE 0x200000U

#define LC_REQ_DYLD 0x80000000U
#define LC_SEGMENT 0x1U
#define LC_SYMTAB 0x2U
#define LC_UNIXTHREAD 0x5U
#define LC_LOAD_DYLIB 0xcU
#define LC_ID_DYLIB 0xdU
#define LC_LOAD_DYLINKER 0xeU
#define LC_LOAD_WEAK_DYLIB (0x18U | LC_REQ_DYLD)
#define LC_SEGMENT_64 0x19U
#define LC_SEGMENT_SPLIT_INFO 0x1eU
#define LC_REEXPORT_DYLIB 0x1fU
#define LC_LAZY_LOAD_DYLIB 0x20U
#define LC_DYLD_INFO 0x22U
#define LC_LOAD_UPWARD_DYLIB 0x23U
#define LC_DYLD_INFO_ONLY (0x22U | LC_REQ_DYLD)
#define LC_MAIN (0x28U | LC_REQ_DYLD)
#define LC_DYLD_EXPORTS_TRIE (0x33U | LC_REQ_DYLD)
#define LC_DYLD_CHAINED_FIXUPS (0x34U | LC_REQ_DYLD)

#define S_ZEROFILL 0x1U
#define S_GB_ZEROFILL 0xcU
#define S_THREAD_LOCAL_ZEROFILL 0x12U

#define N_TYPE 0x0eU
#define N_SECT 0xeU

#define VM_PROT_READ 0x1
#define VM_PROT_WRITE 0x2
#define VM_PROT_EXECUTE 0x4

// Export trie, as emitted into LC_DYLD_EXPORTS_TRIE or into the export_off /
// export_size pair of LC_DYLD_INFO(_ONLY). A dylib has no symbol table the
// loader is meant to walk for exports; this is the index it publishes instead.
#define EXPORT_SYMBOL_FLAGS_KIND_MASK 0x03U
#define EXPORT_SYMBOL_FLAGS_KIND_REGULAR 0x00U
#define EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL 0x01U
#define EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE 0x02U
#define EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION 0x04U
#define EXPORT_SYMBOL_FLAGS_REEXPORT 0x08U
#define EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER 0x10U

// Bind opcodes. These walk the pointer slots an executable left for its imports
// and overwrite each one with the address the symbol actually resolved to.
#define BIND_OPCODE_MASK 0xf0U
#define BIND_IMMEDIATE_MASK 0x0fU
#define BIND_OPCODE_DONE 0x00U
#define BIND_OPCODE_SET_DYLIB_ORDINAL_IMM 0x10U
#define BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB 0x20U
#define BIND_OPCODE_SET_DYLIB_SPECIAL_IMM 0x30U
#define BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40U
#define BIND_OPCODE_SET_TYPE_IMM 0x50U
#define BIND_OPCODE_SET_ADDEND_SLEB 0x60U
#define BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70U
#define BIND_OPCODE_ADD_ADDR_ULEB 0x80U
#define BIND_OPCODE_DO_BIND 0x90U
#define BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB 0xa0U
#define BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED 0xb0U
#define BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB 0xc0U

#define BIND_TYPE_POINTER 1
#define BIND_TYPE_TEXT_ABSOLUTE32 2
#define BIND_TYPE_TEXT_PCREL32 3

// Rebase opcodes. These apply the slide to pointers the image already holds,
// which only matters for a dylib: a non-PIE executable is mapped exactly where
// it was linked, so its slide is zero and rebasing it is a no-op.
#define REBASE_OPCODE_MASK 0xf0U
#define REBASE_IMMEDIATE_MASK 0x0fU
#define REBASE_OPCODE_DONE 0x00U
#define REBASE_OPCODE_SET_TYPE_IMM 0x10U
#define REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x20U
#define REBASE_OPCODE_ADD_ADDR_ULEB 0x30U
#define REBASE_OPCODE_DO_REBASE_IMM_TIMES 0x40U
#define REBASE_OPCODE_DO_REBASE_ULEB_TIMES 0x50U
#define REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB 0x60U
#define REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB 0x70U

typedef int32_t MachOCpuType;
typedef int32_t MachOCpuSubtype;
typedef uint32_t MachOFileType;
typedef uint32_t MachOFlags;
typedef uint32_t MachOLoadCommandType;
typedef uint32_t MachOLoadCommandSize;
typedef int32_t MachOVmProtection;
typedef uint32_t MachOSegmentFlags;
typedef uint32_t MachOSectionType;
typedef uint32_t MachOSectionFlags;

typedef struct MachHeader64 {
  uint32_t                  magic;
  MachOCpuType          cputype;
  MachOCpuSubtype   cpusubtype;
  MachOFileType         filetype;
  uint32_t                  ncmds;
  uint32_t                  sizeofcmds;
  MachOFlags                flags;
  uint32_t                  reserved;
} MachHeader64;

typedef struct MachOLoadCommand {
  MachOLoadCommandType   cmd;
  MachOLoadCommandSize   cmdsize;
} MachOLoadCommand;

typedef struct MachOSegment64 {
  MachOLoadCommandType   cmd;
  MachOLoadCommandSize   cmdsize;
  char                                   segname[16];
  uint64_t                           vmaddr;
  uint64_t                           vmsize;
  uint64_t                           fileoff;
  uint64_t                           filesize;
  MachOVmProtection          maxprot;
  MachOVmProtection          initprot;
  uint32_t                           nsects;
  MachOSegmentFlags          flags;
} MachOSegment64;

typedef struct MachOSection64 {
  char                                   sectname[16];
  char                                   segname[16];
  uint64_t                           addr;
  uint64_t                           size;
  uint32_t                           offset;
  uint32_t                           align;
  uint32_t                           reloff;
  uint32_t                           nreloc;
  MachOSectionFlags          flags;
  uint32_t                           reserved1;
  uint32_t                           reserved2;
  uint32_t                           reserved3;
} MachOSection64;

typedef struct MachOSymtabCommand {
  MachOLoadCommandType cmd;
  MachOLoadCommandSize cmdsize;
  uint32_t                         symoff;
  uint32_t                         nsyms;
  uint32_t                         stroff;
  uint32_t                         strsize;
} MachOSymtabCommand;

typedef struct MachODyldInfoCommand {
  MachOLoadCommandType cmd;
  MachOLoadCommandSize cmdsize;
  uint32_t                         rebase_off;
  uint32_t                         rebase_size;
  uint32_t                         bind_off;
  uint32_t                         bind_size;
  uint32_t                         weak_bind_off;
  uint32_t                         weak_bind_size;
  uint32_t                         lazy_bind_off;
  uint32_t                         lazy_bind_size;
  uint32_t                         export_off;
  uint32_t                         export_size;
} MachODyldInfoCommand;

typedef struct MachOMainCommand {
  MachOLoadCommandType cmd;
  MachOLoadCommandSize cmdsize;
  uint64_t                         entryoff;
  uint64_t                         stacksize;
} MachOMainCommand;

typedef struct MachOThreadCommand {
  MachOLoadCommandType cmd;
  MachOLoadCommandSize cmdsize;
  uint32_t                         flavor;
  uint32_t                         count;
} MachOThreadCommand;

typedef struct MachONlist64 {
  uint32_t n_strx;
  uint8_t  n_type;
  uint8_t  n_sect;
  uint16_t n_desc;
  uint64_t n_value;
} MachONlist64;

#if CONFIG_MHO_EXEC
bool macho_check_file(const void *image, size_t size);
bool macho_is_header(const void *image, size_t size);
Task *machoExecute(char *filepath, uint32_t argc, char **argv, uint32_t envc,
                                   char **envv, bool startup);
#endif

#endif
