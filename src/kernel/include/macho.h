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
