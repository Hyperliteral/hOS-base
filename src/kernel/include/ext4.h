#include "system.h"
#include "types.h"
#include "vfs.h"

#ifndef EXT4_H
#define EXT4_H

// & 0xF000
#define EXT4_S_IFSOCK 0xC000
#define EXT4_S_IFLNK 0xA000
#define EXT4_S_IFREG 0x8000
#define EXT4_S_IFBLK 0x6000
#define EXT4_S_IFDIR 0x4000
#define EXT4_S_IFCHR 0x2000
#define EXT4_S_IFIFO 0x1000

// dir entry file types, they're not the same as the mode bits!
#define EXT4_FT_UNKNOWN 0
#define EXT4_FT_REG_FILE 1
#define EXT4_FT_DIR 2
#define EXT4_FT_CHRDEV 3
#define EXT4_FT_BLKDEV 4
#define EXT4_FT_FIFO 5
#define EXT4_FT_SOCK 6
#define EXT4_FT_SYMLINK 7

#define EXT4_ROOT_INODE 2

// Required (incompat) feature flags, from linux ext4_fs.h
#define EXT4_F_INCOMPAT_FT_DIR 0x0002
#define EXT4_F_INCOMPAT_RECOVER 0x0004
#define EXT4_F_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT4_F_INCOMPAT_META_BG 0x0010
#define EXT4_F_INCOMPAT_EXTENTS 0x0040
#define EXT4_F_INCOMPAT_64BIT 0x0080
#define EXT4_F_INCOMPAT_MMP 0x0100
#define EXT4_F_INCOMPAT_FLEX_BG 0x0200
#define EXT4_F_INCOMPAT_EA_INODE 0x0400
#define EXT4_F_INCOMPAT_DIRDATA 0x1000
#define EXT4_F_INCOMPAT_CSUM_SEED 0x2000
#define EXT4_F_INCOMPAT_LARGEDIR 0x4000
#define EXT4_F_INCOMPAT_INLINE_DATA 0x8000
#define EXT4_F_INCOMPAT_ENCRYPT 0x10000

#define EXT4_SUPPORTED_INCOMPAT                                                                                                \
  (EXT4_F_INCOMPAT_FT_DIR | EXT4_F_INCOMPAT_RECOVER |                                                  \
   EXT4_F_INCOMPAT_EXTENTS | EXT4_F_INCOMPAT_64BIT | EXT4_F_INCOMPAT_FLEX_BG)

// Compatible feature used to signal the presence of a journal
#define EXT4_F_COMPAT_HAS_JOURNAL 0x4

// FileSystem State
#define EXT4_FS_S_CLEAN 1
#define EXT4_FS_S_ERRORS 2

// FileSystem Error handling
#define EXT4_FS_E_IGNORE 1
#define EXT4_FS_E_REMOUNT_RO 2
#define EXT4_FS_E_KPANIC 3

// Inode flags used by this driver
#define EXT4_EXTENTS_FL 0x00080000

// the magic at the top of every extent tree node (inline or in a block)
#define EXT4_EXTENT_MAGIC 0xF30A

// inline extent capacity inside the 60-byte i_block area
#define EXT4_INLINE_EXTENTS_MAX 4

// ---- jbd2 (journal) ----
#define EXT4_JBD2_MAGIC 0xC03B3998u
#define EXT4_JBD2_DESCRIPTOR 1
#define EXT4_JBD2_COMMIT 2
#define EXT4_JBD2_SUPER_V1 3
#define EXT4_JBD2_SUPER_V2 4
#define EXT4_JBD2_REVOKE 5

#define EXT4_JBD2_FLAG_ESCAPE 0x1
#define EXT4_JBD2_FLAG_SAME_UUID 0x2
#define EXT4_JBD2_FLAG_LAST_TAG 0x8

#define EXT4_JBD2_FEATURE_INCOMPAT_REVOKE 0x1

typedef struct Ext4Superblock {
  uint32_t inodes_count;                   // 0x00
  uint32_t blocks_count_lo;                // 0x04
  uint32_t r_blocks_count_lo;          // 0x08
  uint32_t free_blocks_count_lo;   // 0x0c
  uint32_t free_inodes_count;          // 0x10
  uint32_t first_data_block;           // 0x14
  uint32_t log_block_size;                 // 0x18
  uint32_t log_cluster_size;           // 0x1c
  uint32_t blocks_per_group;           // 0x20
  uint32_t clusters_per_group;         // 0x24
  uint32_t inodes_per_group;           // 0x28
  uint32_t mtime;                                  // 0x2c
  uint32_t wtime;                                  // 0x30
  uint16_t mnt_count;                          // 0x34
  uint16_t max_mnt_count;                  // 0x36
  uint16_t magic;                                  // 0x38
  uint16_t state;                                  // 0x3a
  uint16_t errors;                                 // 0x3c
  uint16_t minor_rev_level;                // 0x3e
  uint32_t lastcheck;                          // 0x40
  uint32_t checkinterval;                  // 0x44
  uint32_t creator_os;                         // 0x48
  uint32_t rev_level;                          // 0x4c
  uint16_t def_resuid;                         // 0x50
  uint16_t def_resgid;                         // 0x52
  uint32_t first_ino;                          // 0x54
  uint16_t inode_size;                         // 0x58
  uint16_t block_group_nr;                 // 0x5a
  uint32_t feature_compat;                 // 0x5c
  uint32_t feature_incompat;           // 0x60
  uint32_t feature_ro_compat;          // 0x64
  char         fs_id[16];                          // 0x68
  char         vol_name[16];                   // 0x78
  char         last_mount_path[64];        // 0x88
  uint32_t algorithm_usage_bitmap; // 0xc8
  uint8_t  file_pre_alloc_blocks;  // 0xcc
  uint8_t  dir_pre_alloc_blocks;   // 0xcd
  uint16_t reserved_gdt_blocks;        // 0xce
  char         journal_id[16];                 // 0xd0
  uint32_t journal_inode;                  // 0xe0
  uint32_t journal_device;                 // 0xe4
  uint32_t orphan_head;                        // 0xe8
  uint32_t hash_seed[4];                   // 0xec
  uint8_t  def_hash_version;           // 0xfc
  uint8_t  jnl_backup_type;                // 0xfd
  uint16_t desc_size;                          // 0xfe
  uint32_t default_mount_opts;         // 0x100
  uint32_t first_meta_bg;                  // 0x104
  uint32_t mkfs_time;                          // 0x108
  uint32_t jnl_blocks[17];                 // 0x10c
  uint32_t blocks_count_hi;                // 0x150
  uint32_t r_blocks_count_hi;          // 0x154
  uint32_t free_blocks_count_hi;   // 0x158
  uint16_t min_extra_isize;                // 0x15c
  uint16_t want_extra_isize;           // 0x15e
  uint32_t flags;                                  // 0x160
  uint16_t raid_stride;                        // 0x164
  uint16_t mmp_update_interval;        // 0x166
  uint64_t mmp_block;                          // 0x168
  uint32_t raid_stripe_width;          // 0x170
  uint8_t  log_groups_per_flex;        // 0x174
  uint8_t  checksum_type;                  // 0x175
  uint16_t reserved_pad;                   // 0x176
  uint64_t kbytes_written;                 // 0x178
} __attribute__((packed)) Ext4Superblock;

typedef struct Ext4BlockGroup {
  uint32_t block_bitmap_lo;          // 0x00
  uint32_t inode_bitmap_lo;          // 0x04
  uint32_t inode_table_lo;           // 0x08
  uint16_t free_blocks_count_lo; // 0x0c
  uint16_t free_inodes_count_lo; // 0x0e
  uint16_t used_dirs_count_lo;   // 0x10
  uint16_t flags;                                // 0x12
  uint32_t exclude_bitmap_lo;        // 0x14
  uint16_t block_bitmap_csum_lo; // 0x18
  uint16_t inode_bitmap_csum_lo; // 0x1a
  uint16_t inode_table_csum_lo;  // 0x1c
  uint16_t itable_unused_lo;         // 0x1e
  uint16_t checksum;                         // 0x20
  uint16_t reserved;                         // 0x22
} __attribute__((packed)) Ext4BlockGroup;

typedef struct Ext4ExtentHeader {
  uint16_t magic; // 0xf30a
  uint16_t entries;
  uint16_t max;
  uint16_t depth;
  uint32_t generation;
} __attribute__((packed)) Ext4ExtentHeader;

typedef struct Ext4Extent {
  uint32_t ee_block;        // first logical block
  uint16_t ee_len;          // number of blocks covered
  uint16_t ee_start_hi; // physical start, upper 16 bits
  uint32_t ee_start_lo; // physical start, lower 32 bits
} __attribute__((packed)) Ext4Extent;

typedef struct Ext4ExtentIndex {
  uint32_t ei_block;   // first logical block in the child subtree
  uint32_t ei_leaf_lo; // physical block of the child node
  uint16_t ei_leaf_hi;
  uint16_t ei_unused;
} __attribute__((packed)) Ext4ExtentIndex;

// 128 byte inode (with the 256-byte extension area ignored but preserved)
typedef struct Ext4Inode {
  uint16_t permission;
  uint16_t uid;
  uint32_t size;
  uint32_t atime;
  uint32_t ctime;
  uint32_t mtime;
  uint32_t dtime;
  uint16_t gid;
  uint16_t hard_links;
  uint32_t num_sectors;
  uint32_t flags;
  uint32_t os_specific1;
  uint32_t block[15];
  uint32_t generation;
  uint32_t file_acl;
  union {
        uint32_t dir_acl;
        uint32_t size_high;
  };
  uint32_t f_block_addr;
  uint16_t blocks_high;
  uint16_t file_acl_high;
  uint16_t uid_high;
  uint16_t gid_high;
  uint16_t checksum_lo;
  uint16_t reserved;
  uint16_t extra_isize;
  uint16_t checksum_hi;
  uint32_t ctime_extra;
  uint32_t mtime_extra;
  uint32_t atime_extra;
  uint32_t crtime;
  uint32_t crtime_extra;
  uint32_t version_hi;
  uint32_t projid;
} __attribute__((packed)) Ext4Inode;

typedef struct Ext4Directory {
  uint32_t inode;
  uint16_t size;
  uint8_t  filenameLength;
  uint8_t  type;
  char         filename[0];
} Ext4Directory;

#define EXT4_MAX_CONSEC_DIRALLOC 32
#define EXT4_MAX_CONSEC_BLOCK 32
#define EXT4_MAX_CONSEC_INODE 32
#define EXT4_MAX_CONSEC_WRITE 32

typedef struct Ext4CacheObject {
  struct Ext4CacheObject *next;
  struct Ext4CacheObject *prev;

  uint32_t blockIndex;
  uint8_t *buff;
  uint32_t blocks; // size = this * ext4->blockSize
} Ext4CacheObject;

typedef struct Ext4FoundObject {
  struct LLheader _ll;

  LLcontrol inner; // Ext4FoundObject

  // id
  uint32_t inode;

  atomic_int openFds;

  char *filename;
  int   filenameLen;

  uint64_t hash;

  // global file lock
  SpinlockCnt WLOCK_FILE;

  // cache lock
  SpinlockCnt WLOCK_CACHE;

  // deletion
  char *_Atomic filenameToBeDeleted;

  // caching
  Ext4CacheObject *firstCacheObj;
} Ext4FoundObject;

typedef struct Ext4StagedBlock {
  struct Ext4StagedBlock *next;
  uint32_t                                blockNum;
  uint8_t                                *data; // blockSize copy
} Ext4StagedBlock;

typedef struct Ext4Journal {
  bool active;  // journal present, understood & being used
  bool enabled; // journal present & understood (recovery only even if not
                                // active)

  uint32_t *blocks; // journal inode's physical blocks
  uint32_t  blockCount;
  uint32_t  maxlen;   // total journal blocks
  uint32_t  first;        // first log block
  uint32_t  sequence; // sequence of the transaction at ->start
  uint32_t  start;        // start of oldest un-applied log (== next write pos)

  // in-memory transaction staging
  int                          depth;
  Ext4StagedBlock *staged;
} Ext4Journal;

typedef struct Ext4 {
  // various offsets
  size_t offsetBase;
  size_t offsetSuperblock;
  size_t offsetBGDT;
  size_t blockSize;
  size_t inodeSize;
  size_t inodeSizeRounded;

  // cnts
  size_t blockGroups;

  // block group descriptor entry size (32 without 64bit, 64 with)
  size_t blockGD;
  bool   bgdt64;

  Ext4Superblock superblock;

  uint8_t *bgdts; // regular old array, blockGD stride

  Spinlock  LOCK_OBJECT;
  LLcontrol rootObject; // Ext4FoundObject

  SpinlockCnt WLOCK_GLOBAL_NOFD;

  // regular Spinlock[] array for every bgdt item
  SpinlockCnt *WLOCKS_BLOCK_BITMAP;
  SpinlockCnt *WLOCKS_INODE;

  Spinlock LOCK_BGDT_WRITE;
  Spinlock LOCK_SUPERBLOCK_WRITE;

  Spinlock LOCK_DIRALLOC;

  // scratch pointer: points at ino->block while committing a tree
  uint8_t *rootScratchInode;

  // journal
  Spinlock        LOCK_JOURNAL;
  Ext4Journal journal;
} Ext4;

typedef struct Ext4LookupControl {
  // scratch buffer for walking the extent tree
  uint32_t *tmp1;
  size_t        tmp1Block;
} Ext4LookupControl;

typedef struct Ext4OpenFd {
  Ext4LookupControl lookup;
  // ^ serves as our inodeCurr somewhat

  Ext4FoundObject *globalObject;

  uint64_t ptr;

  uint32_t  inodeNum;
  Ext4Inode inode;
} Ext4OpenFd;

#define EXT4_PTR(a) ((Ext4 *)(a))
#define EXT4_DIR_PTR(a) ((Ext4OpenFd *)(a))

#define EXT4_BLOCK_TO_LBA(ext4, group, block)                                                                  \
  ((ext4)->offsetBase +                                                                                                                \
   ((group) * (ext4)->superblock.blocks_per_group * (ext4)->blockSize) /           \
           SECTOR_SIZE +                                                                                                                   \
   ((block) * (ext4)->blockSize) / SECTOR_SIZE)

#define EXT4_INODE_TO_BLOCK_GROUP(ext4, inode)                                                                 \
  (((inode) - 1) / (ext4)->superblock.inodes_per_group)

#define EXT4_INODE_TO_INDEX(ext4, inode)                                                                           \
  (((inode) - 1) % (ext4)->superblock.inodes_per_group)

// ext4_controller.c
bool   probeExt4(mbr_partition *mbr);
bool   ext4Mount(MountPoint *mount);
size_t ext4Open(char *filename, int flags, int mode, OpenFile *fd,
                                char **symlinkResolve);
bool   ext4Close(OpenFile *fd);
size_t ext4Read(OpenFile *fd, uint8_t *buff, size_t limit);
size_t ext4ReadInner(OpenFile *fd, uint8_t *buff, size_t limit);
bool   ext4Stat(MountPoint *mnt, char *filename, struct stat *target,
                                char **symlinkResolve);
bool   ext4Lstat(MountPoint *mnt, char *filename, struct stat *target,
                                 char **symlinkResolve);
size_t ext4StatFd(OpenFile *fd, struct stat *target);
size_t ext4Seek(OpenFile *fd, size_t target, long int offset, int whence);
size_t ext4GetFilesize(OpenFile *fd);
size_t ext4Readlink(MountPoint *mnt, char *path, char *buf, int size,
                                        char **symlinkResolve);
size_t ext4Delete(MountPoint *mnt, char *filename, bool directory,
                                  char **symlinkResolve);
size_t ext4Link(MountPoint *mnt, char *filename, char *target,
                                char **symlinkResolve, char **symlinkResolveTarget);

// ext4_create.c
size_t ext4Mkdir(MountPoint *mnt, char *dirname, uint32_t mode,
                                 char **symlinkResolve);
size_t ext4Touch(MountPoint *mnt, char *filename, uint32_t mode,
                                 char **symlinkResolve);

// ext4_util.c
void ext4BlockFetchInit(Ext4 *ext4, Ext4LookupControl *control);
void ext4BlockFetchCleanup(Ext4LookupControl *control);

size_t ext4BlockSizeCalculate(Ext4 *ext4, size_t raw);

// extent engine
int  ext4ExtentFlatten(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                                           Ext4LookupControl *control, Ext4Extent **out);
int  ext4ExtentMerge(Ext4Extent *list, int count);
void ext4ExtentCommit(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                                          Ext4LookupControl *control, Ext4Extent *list, int count,
                                          uint64_t *blocksUsedOut);
void ext4ExtentInsertBlock(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                                                   Ext4LookupControl *control, uint32_t logical,
                                                   uint64_t physical);

uint64_t  ext4BlockFetch(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                                                 Ext4LookupControl *control, size_t curr);
uint64_t *ext4BlockChain(Ext4 *ext4, Ext4OpenFd *fd, size_t curr,
                                                 size_t blocks);

uint32_t ext4BlockFind(Ext4 *ext4, int groupSuggestion, uint32_t amnt);
uint32_t ext4BlockFindL(Ext4 *ext4, int group, uint32_t amnt);
void         ext4BlockDelete(Ext4 *ext4, uint32_t group, uint32_t index);

// ext4_traverse.c
uint32_t ext4Traverse(Ext4 *ext4, size_t initInode, char *search,
                                          size_t searchLength);
uint32_t ext4TraversePath(Ext4 *ext4, char *path, Ext4FoundObject **retObj,
                                                  bool follow, char **symlinkResolve);

// ext4_inode.c
Ext4Inode *ext4InodeFetch(Ext4 *ext4, size_t inode);
void           ext4InodeModifyM(Ext4 *ext4, size_t inode, Ext4Inode *target);
void           ext4InodeDelete(Ext4 *ext4, size_t inode);

uint32_t ext4InodeFindL(Ext4 *ext4, int group);
uint32_t ext4InodeFind(Ext4 *ext4, int groupSuggestion);

// ext4_dirs.c
bool   ext4DirAllocate(Ext4 *ext4, uint32_t inodeNum, Ext4Inode *parentDirInode,
                                           char *filename, uint8_t filenameLen, uint8_t type,
                                           uint32_t inode);
size_t ext4Getdents64(OpenFile *file, struct linux_dirent64 *start,
                                          unsigned int hardlimit);

void ext4BgdtPushM(Ext4 *ext4);
void ext4SuperblockPushM(Ext4 *ext4);

// ext4_util.c shared helpers
uint64_t ext4SuperBlockCount(Ext4 *ext4);
uint64_t ext4BgBlockBitmap(Ext4 *ext4, size_t i);
uint64_t ext4BgInodeBitmap(Ext4 *ext4, size_t i);
uint64_t ext4BgInodeTable(Ext4 *ext4, size_t i);
uint64_t ext4BgFreeBlocks(Ext4 *ext4, size_t i);
uint64_t ext4BgFreeInodes(Ext4 *ext4, size_t i);
bool         ext4DirRemove(Ext4 *ext4, Ext4Inode *parentDirInode,
                                           uint32_t parentDirInodeNum, char *filename,
                                           uint8_t filenameLen);

// ext4_caching.c
void ext4CacheAddSecurely(MountPoint *mnt, Ext4FoundObject *global,
                                                  uint8_t *buff, size_t blockIndex, size_t blocks);
void ext4CachePush(Ext4 *ext4, Ext4OpenFd *fd);

// ext4_journal.c
void ext4JournalInit(Ext4 *ext4);
void ext4JournalBegin(Ext4 *ext4);
void ext4JournalAbort(Ext4 *ext4);
void ext4JournalCommit(Ext4 *ext4);
void ext4StageMetadata(Ext4 *ext4, size_t lba, uint8_t *data, size_t sectors);

// finale
VfsHandlers ext4Handlers;

#endif