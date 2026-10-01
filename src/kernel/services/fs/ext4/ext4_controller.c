#include <bootloader.h>
#include <ext4.h>
#include <malloc.h>
#include <paging.h>
#include <string.h>
#include <syscalls.h>
#include <system.h>
#include <task.h>
#include <timer.h>
#include <util.h>
#include <vmm.h>

bool probeExt4(mbr_partition *mbr) {
  uint8_t tmp[1024] __attribute__((aligned(2))) = {0};
  getDiskBytes(tmp, mbr->lba_first_sector + 2, 2);

  uint16_t *magic = (uint16_t *)&tmp[0x38];
  uint32_t *rev = (uint32_t *)&tmp[0x4c];
  uint32_t *incompat = (uint32_t *)&tmp[0x60];

  if (*magic != 0xEF53 || *rev < 1)
    return false;

  // ext4 places its distinguishing hashnads in the *required* feature set
  if (*incompat & (EXT4_F_INCOMPAT_EXTENTS | EXT4_F_INCOMPAT_64BIT |
                   EXT4_F_INCOMPAT_FLEX_BG | EXT4_F_INCOMPAT_META_BG))
    return true;

  return false;
}

// allocates & resolves a logical block to a physical one, handing out free
// blocks for any holes, appending brand new extents to the working list
static uint64_t ext4WriteResolve(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                                 Ext4LookupControl *control,
                                 Ext4Extent *extents, int extentCount,
                                 uint32_t logical, bool *allocatedAny) {
  for (int i = 0; i < extentCount; i++) {
    if (logical >= extents[i].ee_block &&
        logical < (uint32_t)(extents[i].ee_block + extents[i].ee_len)) {
      uint64_t start =
          ((uint64_t)extents[i].ee_start_hi << 32) | extents[i].ee_start_lo;
      return start + (logical - extents[i].ee_block);
    }
  }

  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inodeNum);
  uint64_t phys = ext4BlockFind(ext4, group, 1);
  if (!phys)
    return 0;

  extents[extentCount].ee_block = logical;
  extents[extentCount].ee_len = 1;
  extents[extentCount].ee_start_hi = phys >> 32;
  extents[extentCount].ee_start_lo = phys;
  *allocatedAny = true;

  (void)ino;
  (void)control;
  return phys;
}

bool ext4Mount(MountPoint *mount) {
  // assign handlers
  mount->handlers = &ext4Handlers;
  mount->stat = ext4Stat;
  mount->lstat = ext4Lstat;

  mount->mkdir = ext4Mkdir;
  mount->delete = ext4Delete;
  mount->readlink = ext4Readlink;
  mount->link = ext4Link;

  // assign fsInfo
  mount->fsInfo = malloc(sizeof(Ext4));
  memset(mount->fsInfo, 0, sizeof(Ext4));
  Ext4 *ext4 = EXT4_PTR(mount->fsInfo);

  // base offset
  ext4->offsetBase = mount->mbr.lba_first_sector;
  ext4->offsetSuperblock = mount->mbr.lba_first_sector + 2;

  // get superblock (it's a full 1024 bytes)
  uint8_t tmp[1024] __attribute__((aligned(2))) = {0};
  getDiskBytes(tmp, ext4->offsetSuperblock, 2);

  // store it
  memcpy(&ext4->superblock, tmp, sizeof(Ext4Superblock));

  // checks
  if (ext4->superblock.magic != 0xEF53) {
    debugf("[ext4] Invalid magic number!\n");
    goto error;
  }

  if (ext4->superblock.rev_level < 1) {
    debugf(
        "[ext4] FATAL! Ancient, pre-historic ext2 partition discovered! Please "
        "contact your local museum for further info...\n");
    goto error;
  }

  if (ext4->superblock.feature_incompat & ~EXT4_SUPPORTED_INCOMPAT) {
    debugf("[ext4] FATAL! Unsupported required features! {incompat:%x}\n",
           ext4->superblock.feature_incompat);
    goto error;
  }

  if (!(ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_EXTENTS)) {
    debugf("[ext4] FATAL! Volume has no extents feature, that's just ext2!\n");
    goto error;
  }

  if (ext4->superblock.state != EXT4_FS_S_CLEAN) {
    if (ext4->superblock.errors == EXT4_FS_E_REMOUNT_RO) {
      debugf("[ext4] FATAL! Read-only partition!\n");
      goto error;
    } else if (ext4->superblock.errors == EXT4_FS_E_KPANIC) {
      debugf("[ext4] FATAL! Superblock error caused panic!\n");
      panic();
    }
  }

  ext4->blockSize = 1024 << ext4->superblock.log_block_size;

  if ((ext4->blockSize % SECTOR_SIZE) != 0) {
    debugf("[ext4] FATAL! Block size is not sector-aligned! blockSize{%d}\n",
           ext4->blockSize);
    goto error;
  }

  // block group descriptor entry size
  ext4->blockGD = ext4->superblock.desc_size;
  if (ext4->blockGD < sizeof(Ext4BlockGroup))
    ext4->blockGD = sizeof(Ext4BlockGroup);
  ext4->bgdt64 = (ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_64BIT) ||
                 ext4->blockGD > sizeof(Ext4BlockGroup);

  // calculate block groups
  uint64_t totalBlocks = ext4SuperBlockCount(ext4);
  uint64_t blockGroups1 =
      DivRoundUp(totalBlocks, ext4->superblock.blocks_per_group);
  uint64_t blockGroups2 = DivRoundUp(ext4->superblock.inodes_count,
                                     ext4->superblock.inodes_per_group);
  if (blockGroups1 != blockGroups2) {
    debugf("[ext4] Total block group calculation doesn't match up! 1{%ld} "
           "2{%ld}\n",
           blockGroups1, blockGroups2);
    goto error;
  }
  ext4->blockGroups = blockGroups1;

  // find the Block Group Descriptor Table
  // remember, very max is block size
  ext4->offsetBGDT =
      EXT4_BLOCK_TO_LBA(ext4, 0, ext4->superblock.first_data_block + 1);
  ext4->bgdts = (uint8_t *)malloc(ext4->blockGroups * ext4->blockGD);
  getDiskBytes(ext4->bgdts, ext4->offsetBGDT,
               DivRoundUp(ext4->blockGroups * ext4->blockGD, SECTOR_SIZE));

  // set up counting spinlocks for the BGDTs
  // remember to zero them, just in case
  int bgdtLockSize = sizeof(SpinlockCnt) * ext4->blockGroups;
  ext4->WLOCKS_BLOCK_BITMAP = (SpinlockCnt *)malloc(bgdtLockSize);
  memset(ext4->WLOCKS_BLOCK_BITMAP, 0, bgdtLockSize);

  ext4->WLOCKS_INODE = (SpinlockCnt *)malloc(bgdtLockSize);
  memset(ext4->WLOCKS_INODE, 0, bgdtLockSize);

  ext4->inodeSize = ext4->superblock.inode_size;
  ext4->inodeSizeRounded =
      DivRoundUp(ext4->inodeSize, SECTOR_SIZE) * SECTOR_SIZE;

  // root found object
  LinkedListInit(&ext4->rootObject, sizeof(Ext4FoundObject));
  Ext4FoundObject *root =
      LinkedListAllocate(&ext4->rootObject, sizeof(Ext4FoundObject));
  root->inode = EXT4_ROOT_INODE;
  root->openFds = 69420;
  root->filename = 0; // null
  LinkedListInit(&root->inner, sizeof(Ext4FoundObject));

  // this is a good moment to touch the journal: any pending transaction gets
  // replayed, and if the fs was marked with a pending recovery we clear it now
  ext4JournalInit(ext4);
  if (ext4->journal.active &&
      (ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_RECOVER)) {
    ext4->superblock.feature_incompat &= ~EXT4_F_INCOMPAT_RECOVER;
    ext4SuperblockPushM(ext4);
  }

  // done :")
  return true;

error:
  free(ext4);
  return false;
}

size_t ext4Open(char *filename, int flags, int mode, OpenFile *fd,
                char **symlinkResolve) {
  Ext4 *ext4 = EXT4_PTR(fd->mountPoint->fsInfo);

  size_t ret = 0;
  spinlockCntReadAcquire(&ext4->WLOCK_GLOBAL_NOFD);

  Ext4FoundObject *obj = 0;
  uint32_t inode = ext4TraversePath(ext4, filename, &obj, true, symlinkResolve);

  if (!inode && *symlinkResolve) {
    // last entry is a soft symlink that'll have to be resolved back on the
    // open() phase..
    if (flags & O_NOFOLLOW) {
      ret = ERR(ELOOP);
      goto cleanup;
    } else {
      ret = ERR(ENOENT);
      goto cleanup;
    }
  }

  if (inode && flags & O_EXCL && flags & O_CREAT) {
    ret = ERR(EEXIST);
    goto cleanup;
  }

  if (!inode) {
    if (flags & O_CREAT) {
      // escalate permissions & ensure nobody created this before us
      spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
      spinlockCntWriteAcquire(&ext4->WLOCK_GLOBAL_NOFD);
      if (!(inode =
                ext4TraversePath(ext4, filename, &obj, true, symlinkResolve))) {
        // create this thing
        size_t touch =
            ext4Touch(fd->mountPoint, filename, mode, symlinkResolve);
        if (RET_IS_ERR(touch)) {
          ret = touch;
          spinlockCntWriteRelease(&ext4->WLOCK_GLOBAL_NOFD);
          spinlockCntReadAcquire(&ext4->WLOCK_GLOBAL_NOFD);
          goto cleanup;
        }

        // created successfully
        inode = ext4TraversePath(ext4, filename, &obj, true, symlinkResolve);
      }
      spinlockCntWriteRelease(&ext4->WLOCK_GLOBAL_NOFD);
      spinlockCntReadAcquire(&ext4->WLOCK_GLOBAL_NOFD);
    } else {
      ret = ERR(ENOENT);
      goto cleanup;
    }
  }

  Ext4Inode *inodeFetched = ext4InodeFetch(ext4, inode);
  if (flags & O_DIRECTORY && !(inodeFetched->permission & S_IFDIR)) {
    ret = ERR(ENOTDIR);
    goto cleanup_free;
  }

  if (flags & O_TRUNC) {
    ext4JournalBegin(ext4);
    inodeFetched->size = 0;
    inodeFetched->size_high = 0;
    inodeFetched->num_sectors = 0;
    inodeFetched->blocks_high = 0;

    ext4InodeModifyM(ext4, inode, inodeFetched);
    ext4JournalCommit(ext4);

    // todo: free the needed data blocks
  }

  // we opened a file!
  assert(obj);
  // openfds has been already increased on the lookup call!

  Ext4OpenFd *dir = (Ext4OpenFd *)malloc(sizeof(Ext4OpenFd));
  memset(dir, 0, sizeof(Ext4OpenFd));
  fd->dir = dir;

  dir->inodeNum = inode;
  memcpy(&dir->inode, inodeFetched, sizeof(Ext4Inode));

  dir->globalObject = obj;

  if ((dir->inode.permission & 0xF000) == EXT4_S_IFDIR) {
    size_t len = strlength(filename) + 1;
    fd->dirname = malloc(len);
    memcpy(fd->dirname, filename, len);
  }

  ext4BlockFetchInit(ext4, &dir->lookup);

  // pointers & stuff
  dir->ptr = 0;

cleanup_free:
  free(inodeFetched);

cleanup:
  spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
  return ret;
}

size_t ext4Read(OpenFile *fd, uint8_t *buff, size_t naiveLimit) {
  Ext4       *ext4 = EXT4_PTR(fd->mountPoint->fsInfo);
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);

  if (dir->inode.permission & S_IFDIR)
    return ERR(EISDIR);

  ext4CachePush(ext4, dir);

  size_t filesize = ext4GetFilesize(fd);
  if (dir->ptr >= filesize)
    return 0;

  size_t limit = naiveLimit;
  if (limit > (filesize - dir->ptr))
    limit = filesize - dir->ptr;

  size_t blocksRequired = DivRoundUp(limit, ext4->blockSize);

  spinlockCntReadAcquire(&dir->globalObject->WLOCK_FILE);

  // find anything as a start that is close
  spinlockCntReadAcquire(&dir->globalObject->WLOCK_CACHE);
  size_t           blockIndexStart = dir->ptr / ext4->blockSize;
  Ext4CacheObject *cacheObj = dir->globalObject->firstCacheObj;
  while (cacheObj) {
    if (cacheObj->blockIndex >= blockIndexStart &&
        cacheObj->blockIndex < (blockIndexStart + blocksRequired))
      break;
    cacheObj = cacheObj->next;
  }
  spinlockCntReadRelease(&dir->globalObject->WLOCK_CACHE);

  size_t left = limit;
  for (size_t i = 0; i < (blocksRequired + 1); i++) {
    if (cacheObj && cacheObj->blockIndex == (blockIndexStart + i)) {
      // we are in a valid cache region
      spinlockCntReadAcquire(&dir->globalObject->WLOCK_CACHE);
      uint32_t rem = dir->ptr % ext4->blockSize;
      size_t   toCopy = MIN(left, cacheObj->blocks * ext4->blockSize - rem);
      memcpy(&buff[limit - left], &cacheObj->buff[rem], toCopy);
      left -= toCopy;
      i += cacheObj->blocks - 1; // -1 cause it's added automatically
      dir->ptr += toCopy;
      cacheObj = cacheObj->next;
      spinlockCntReadRelease(&dir->globalObject->WLOCK_CACHE);
    } else {
      // we are not inside caching, let's see if we're close to it
      size_t blocksToScan = blocksRequired - i;
      if (!blocksToScan) // the extra block caused by the rem thing
        blocksToScan = 1;
      if (cacheObj)
        // to_be_scanned = target_location - current_location;
        blocksToScan = cacheObj->blockIndex - (dir->ptr / ext4->blockSize);
      uint32_t rem = dir->ptr % ext4->blockSize;
      size_t   toCopy = MIN(left, blocksToScan * ext4->blockSize - rem);
      assert(ext4ReadInner(fd, &buff[limit - left], toCopy) == toCopy);
      left -= toCopy;
      i += blocksToScan - 1; // -1 cause it's added automatically
    }
    if (left == 0)
      break;
  }

  assert(left == 0);
  spinlockCntReadRelease(&dir->globalObject->WLOCK_FILE);
  return limit;
}

size_t ext4ReadInner(OpenFile *fd, uint8_t *buff, size_t limit) {
  Ext4       *ext4 = EXT4_PTR(fd->mountPoint->fsInfo);
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);

  size_t    blocksRequired = DivRoundUp(limit, ext4->blockSize);
  uint64_t *blocks =
      ext4BlockChain(ext4, dir, dir->ptr / ext4->blockSize, blocksRequired);
  size_t tmpSize =
      DivRoundUp((blocksRequired + 1) * ext4->blockSize, BLOCK_SIZE);
  uint8_t *tmp = (uint8_t *)VirtualAllocate(tmpSize);
  int      currBlock = 0;

  // optimization: we can use consecutive sectors to make our life easier
  int consecStart = -1;
  int consecEnd = 0;

  // +1 for starting
  for (int i = 0; i < (blocksRequired + 1); i++) {
    if (!blocks[i])
      break;
    bool last = i == (blocksRequired - 1);
    if (consecStart < 0) {
      // nothing consecutive yet
      if (!last && blocks[i + 1] == (blocks[i] + 1)) {
        // consec starts here
        consecStart = i;
        continue;
      }
    } else {
      // we are in a consecutive that started since consecStart
      if (last || blocks[i + 1] != (blocks[i] + 1))
        consecEnd = i; // either last or the end
      else             // otherwise, we good
        continue;
    }

    if (consecEnd) {
      // optimized consecutive cluster reading
      int needed = consecEnd - consecStart + 1;
      getDiskBytes(&tmp[currBlock * ext4->blockSize],
                   EXT4_BLOCK_TO_LBA(ext4, 0, blocks[consecStart]),
                   (needed * ext4->blockSize) / SECTOR_SIZE);
      currBlock += needed;
    } else {
      getDiskBytes(&tmp[currBlock * ext4->blockSize],
                   EXT4_BLOCK_TO_LBA(ext4, 0, blocks[i]),
                   ext4->blockSize / SECTOR_SIZE);
      currBlock++;
    }

    // traverse
    consecStart = -1;
    consecEnd = 0;
  }

  // actually fill the buffer
  uint32_t offsetStarting = dir->ptr % ext4->blockSize; // remainder
  size_t   headtoCopy = MIN(ext4->blockSize - offsetStarting, limit);
  memcpy(buff, &tmp[offsetStarting], headtoCopy);
  if (limit > headtoCopy) {
    memcpy(&buff[headtoCopy], &tmp[ext4->blockSize], limit - headtoCopy);
  }

  // cache it for later
  ext4CacheAddSecurely(fd->mountPoint, dir->globalObject, tmp,
                       dir->ptr / ext4->blockSize, blocksRequired);

  dir->ptr += limit; // set pointer

  // cleanup
  free(blocks);

  return limit;
}

size_t ext4Write(OpenFile *fd, uint8_t *buff, size_t limit) {
  Ext4       *ext4 = EXT4_PTR(fd->mountPoint->fsInfo);
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);

  ext4CachePush(ext4, dir);

  // todo: also write to cache instead of discarding altogether
  spinlockCntWriteAcquire(&dir->globalObject->WLOCK_CACHE);
  Ext4CacheObject *browse = dir->globalObject->firstCacheObj;
  while (browse) {
    Ext4CacheObject *next = browse->next;
    VirtualFree(browse->buff,
                DivRoundUp((browse->blocks + 1) * ext4->blockSize, BLOCK_SIZE));
    free(browse);
    browse = next;
  }
  dir->globalObject->firstCacheObj = 0;
  spinlockCntWriteRelease(&dir->globalObject->WLOCK_CACHE);

  if (dir->inode.permission & S_IFDIR)
    return ERR(EISDIR);

  ext4JournalBegin(ext4);

  spinlockCntWriteAcquire(&dir->globalObject->WLOCK_FILE);

  size_t appendCursor = (size_t)(-1);
  if (fd->flags & O_APPEND) {
    appendCursor = dir->ptr;
    dir->ptr = COMBINE_64(dir->inode.size_high, dir->inode.size);
  }

  int ptrIgnoredBlocks = dir->ptr / ext4->blockSize;
  int ptrIgnoredBytes = dir->ptr % ext4->blockSize;

  // establish the working extent list, growing it by however many blocks this
  // write can possibly touch
  Ext4Extent *extents = 0;
  int         extentCount = ext4ExtentFlatten(ext4, &dir->inode, dir->inodeNum,
                                              &dir->lookup, &extents);
  if (extentCount < 0) {
    debugf("[ext4::write] FATAL! Corrupt extent tree!\n");
    panic();
  }
  int         maxNew = DivRoundUp(limit, ext4->blockSize) + 1;
  Ext4Extent *working =
      (Ext4Extent *)malloc((extentCount + maxNew) * sizeof(Ext4Extent));
  memcpy(working, extents, extentCount * sizeof(Ext4Extent));
  if (extents)
    free(extents);
  int  workingCount = extentCount;
  bool allocatedAny = false;

  size_t remainder = limit;
  size_t left = 0;

  if (ptrIgnoredBytes > 0) {
    left = MIN(ext4->blockSize - ptrIgnoredBytes, remainder);
    uint64_t block = ext4WriteResolve(ext4, &dir->inode, dir->inodeNum,
                                      &dir->lookup, working, workingCount,
                                      ptrIgnoredBlocks, &allocatedAny);
    if (block)
      workingCount++; // resolve appended an extent for this hole
    uint8_t *tmp = (uint8_t *)malloc(ext4->blockSize);
    getDiskBytes(tmp, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);
    memcpy(&tmp[ptrIgnoredBytes], buff, left);
    setDiskBytes(tmp, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);

    free(tmp);
    remainder -= left;
    dir->ptr += left;

    // ignore the first block in the functions that follow
    ptrIgnoredBlocks++;
  }

  if (remainder > 0) {
    // we are aligned on block boundaries so we can use this
    int       blocksRequired = DivRoundUp(remainder, ext4->blockSize);
    uint64_t *blocks =
        (uint64_t *)malloc((1 + blocksRequired) * sizeof(uint64_t));

    // resolve (and allocate) every block we need
    for (int i = 0; i < (1 + blocksRequired); i++) {
      uint64_t phys = ext4WriteResolve(ext4, &dir->inode, dir->inodeNum,
                                       &dir->lookup, working, workingCount,
                                       ptrIgnoredBlocks + i, &allocatedAny);
      if (phys)
        workingCount++; // appended a fresh extent
      blocks[i] = phys;
    }

    size_t tmpSize =
        DivRoundUp((blocksRequired + 1) * ext4->blockSize, BLOCK_SIZE);
    uint8_t *tmp = (uint8_t *)VirtualAllocate(tmpSize);

    // our first block will have junk data in the start!
    getDiskBytes(tmp, EXT4_BLOCK_TO_LBA(ext4, 0, blocks[0]),
                 ext4->blockSize / SECTOR_SIZE);

    // the last block might have junk data at the end!
    int target = blocksRequired - 1;
    if (target > 0)
      getDiskBytes(&tmp[target * ext4->blockSize],
                   EXT4_BLOCK_TO_LBA(ext4, 0, blocks[target]),
                   ext4->blockSize / SECTOR_SIZE);
    memcpy(tmp, &buff[left], remainder);

    int currBlock = 0;

    // optimization: we can use consecutive sectors to make our life easier
    int consecStart = -1;
    int consecEnd = 0;

    // +1 for starting
    for (int i = 0; i < blocksRequired; i++) {
      if (!blocks[i]) {
        debugf("[ext4::write] FATAL! Out of sync!\n");
        panic();
      }

      bool last = i == (blocksRequired - 1);
      if (consecStart < 0) {
        // nothing consecutive yet
        if (!last && blocks[i + 1] == (blocks[i] + 1)) {
          // consec starts here
          consecStart = i;
          continue;
        }
      } else {
        // we are in a consecutive that started since consecStart
        if (last || blocks[i + 1] != (blocks[i] + 1))
          consecEnd = i; // either last or the end
        else             // otherwise, we good
          continue;
      }

      if (consecEnd) {
        // optimized consecutive cluster reading
        int needed = consecEnd - consecStart + 1;
        setDiskBytes(&tmp[currBlock * ext4->blockSize],
                     EXT4_BLOCK_TO_LBA(ext4, 0, blocks[consecStart]),
                     (needed * ext4->blockSize) / SECTOR_SIZE);
        currBlock += needed;
      } else {
        setDiskBytes(&tmp[currBlock * ext4->blockSize],
                     EXT4_BLOCK_TO_LBA(ext4, 0, blocks[i]),
                     ext4->blockSize / SECTOR_SIZE);
        currBlock++;
      }

      // traverse
      consecStart = -1;
      consecEnd = 0;
    }

    dir->ptr += remainder; // set pointer

    // cleanup
    free(blocks);
    VirtualFree(tmp, tmpSize);
  }

  // commit the new extent tree (only when we've grown/modified it)
  if (allocatedAny) {
    int merged = ext4ExtentMerge(working, workingCount);
    ext4ExtentCommit(ext4, &dir->inode, dir->inodeNum, &dir->lookup, working,
                     merged, 0);
    uint64_t newSize = COMBINE_64(dir->inode.size_high, dir->inode.size);
    ext4InodeModifyM(ext4, dir->inodeNum, &dir->inode);
    (void)newSize;
  }
  free(working);

  if (dir->ptr > COMBINE_64(dir->inode.size_high, dir->inode.size)) {
    // update size
    dir->inode.size = dir->ptr;
    dir->inode.size_high = dir->ptr >> 32;
    if (!allocatedAny)
      dir->inode.num_sectors =
          ext4BlockSizeCalculate(ext4, dir->ptr) / SECTOR_SIZE;
    ext4InodeModifyM(ext4, dir->inodeNum, &dir->inode);
  }

  if (fd->flags & O_APPEND)
    dir->ptr = appendCursor;

  spinlockCntWriteRelease(&dir->globalObject->WLOCK_FILE);

  ext4JournalCommit(ext4);

  return limit;
}

size_t ext4Seek(OpenFile *fd, size_t target, long int offset, int whence) {
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);

  // "hack" because openfile ptr is not used
  if (whence == SEEK_CURR)
    target += dir->ptr;

  size_t filesize = ext4GetFilesize(fd);
  if (target > filesize) {
    if (!(fd->flags & O_RDWR) && !(fd->flags & O_WRONLY))
      return ERR(EINVAL);

    size_t   remainder = target - filesize;
    uint8_t *bytePlacement = calloc(remainder, 1);

    dir->ptr = filesize;
    int written = ext4Write(fd, bytePlacement, remainder);
    if (written != remainder) {
      debugf("[ext4::seek] FAILED! Write not in sync!!\n");
      panic();
    }

    if (dir->ptr != target) {
      debugf("[ext4::seek] What?\n");
      panic();
    }

    free(bytePlacement);
  }
  dir->ptr = target;

  return dir->ptr;
}

size_t ext4GetFilesize(OpenFile *fd) {
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);
  return COMBINE_64(dir->inode.size_high, dir->inode.size);
}

void ext4StatInternal(Ext4 *ext4, Ext4Inode *inode, uint32_t inodeNum,
                      struct stat *target) {
  target->st_dev = 69; // todo
  target->st_ino = inodeNum;
  target->st_mode = inode->permission;
  target->st_nlink = inode->hard_links;
  target->st_blksize = ext4->blockSize;

  target->st_uid = inode->uid;
  target->st_gid = inode->gid;

  target->st_size = COMBINE_64(inode->size_high, inode->size);

  target->st_blocks =
      (DivRoundUp(target->st_size, target->st_blksize) * target->st_blksize) /
      512;

  target->st_atime = inode->atime;
  target->st_mtime = inode->mtime;
  target->st_ctime = inode->ctime;
}

bool ext4Stat(MountPoint *mnt, char *filename, struct stat *target,
              char **symlinkResolve) {
  Ext4 *ext4 = EXT4_PTR(mnt->fsInfo);

  spinlockCntReadAcquire(&ext4->WLOCK_GLOBAL_NOFD);
  uint32_t inodeNum = ext4TraversePath(ext4, filename, 0, true, symlinkResolve);
  if (!inodeNum) {
    spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
    return false;
  }
  Ext4Inode *inode = ext4InodeFetch(ext4, inodeNum);

  ext4StatInternal(ext4, inode, inodeNum, target);

  free(inode);
  spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
  return true;
}

bool ext4Lstat(MountPoint *mnt, char *filename, struct stat *target,
               char **symlinkResolve) {
  Ext4 *ext4 = EXT4_PTR(mnt->fsInfo);

  spinlockCntReadAcquire(&ext4->WLOCK_GLOBAL_NOFD);
  uint32_t inodeNum =
      ext4TraversePath(ext4, filename, 0, false, symlinkResolve);
  if (!inodeNum) {
    spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
    return false;
  }
  Ext4Inode *inode = ext4InodeFetch(ext4, inodeNum);

  ext4StatInternal(ext4, inode, inodeNum, target);

  free(inode);
  spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
  return true;
}

size_t ext4StatFd(OpenFile *fd, struct stat *target) {
  Ext4       *ext4 = EXT4_PTR(fd->mountPoint->fsInfo);
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);
  ext4StatInternal(ext4, &dir->inode, dir->inodeNum, target);
  return 0;
}

size_t ext4Readlink(MountPoint *mnt, char *path, char *buf, int size,
                    char **symlinkResolve) {
  Ext4 *ext4 = EXT4_PTR(mnt->fsInfo);
  if (size < 0)
    return ERR(EINVAL);
  else if (!size)
    return 0;

  size_t     ret = -1;
  Ext4Inode *inode = 0;
  spinlockCntReadAcquire(&ext4->WLOCK_GLOBAL_NOFD);

  uint32_t inodeNum = ext4TraversePath(ext4, path, 0, false, symlinkResolve);
  if (!inodeNum) {
    ret = ERR(ENOENT);
    goto cleanup;
  }

  inode = ext4InodeFetch(ext4, inodeNum);
  if ((inode->permission & 0xF000) != EXT4_S_IFLNK) {
    ret = ERR(EINVAL);
    goto cleanup;
  }

  char *start = (char *)inode->block;
  if (COMBINE_64(inode->size_high, inode->size) > 60) {
    assert(COMBINE_64(inode->size_high, inode->size) < ext4->blockSize);
    start = calloc(ext4->blockSize + 1, 1);
    Ext4LookupControl control = {0};
    ext4BlockFetchInit(ext4, &control);
    uint64_t block = ext4BlockFetch(ext4, inode, inodeNum, &control, 0);
    getDiskBytes((uint8_t *)start, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);
    ext4BlockFetchCleanup(&control);
  }

  int toCopy = COMBINE_64(inode->size_high, inode->size);
  if (toCopy > size)
    toCopy = size;

  memcpy(buf, start, toCopy);
  ret = toCopy;

  if (COMBINE_64(inode->size_high, inode->size) > 60)
    free(start);

cleanup:
  if (inode)
    free(inode);
  spinlockCntReadRelease(&ext4->WLOCK_GLOBAL_NOFD);
  return ret;
}

bool ext4Close(OpenFile *fd) {
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);

  ext4BlockFetchCleanup(&dir->lookup);

  atomic_fetch_sub(&dir->globalObject->openFds, 1);
  char *filenameToBeDeleted =
      (char *)atomic_load(&dir->globalObject->filenameToBeDeleted);
  if (!atomic_load(&dir->globalObject->openFds) && filenameToBeDeleted)
    ext4Delete(fd->mountPoint, filenameToBeDeleted, false, 0);

  free(fd->dir);
  return true;
}

// todo: after all locking has been correctly done, this needs to just share the
// same ->dir. Yes, yes, even on fork()s
bool ext4DuplicateNodeUnsafe(OpenFile *original, OpenFile *orphan) {
  orphan->dir = malloc(sizeof(Ext4OpenFd));
  memcpy(orphan->dir, original->dir, sizeof(Ext4OpenFd));

  Ext4       *ext4 = EXT4_PTR(orphan->mountPoint->fsInfo);
  Ext4OpenFd *dir = EXT4_DIR_PTR(orphan->dir);
  Ext4OpenFd *dirOriginal = EXT4_DIR_PTR(original->dir);

  if (dir->lookup.tmp1) {
    dir->lookup.tmp1 = malloc(ext4->blockSize);
    memcpy(dir->lookup.tmp1, dirOriginal->lookup.tmp1, ext4->blockSize);
  }

  if (original->dirname) {
    size_t len = strlength(original->dirname) + 1;
    orphan->dirname = (char *)malloc(len);
    memcpy(orphan->dirname, original->dirname, len);
  }

  atomic_fetch_add(&dirOriginal->globalObject->openFds, 1);

  return true;
}

// task is taken into account
size_t ext4Mmap(size_t addr, size_t length, int prot, int flags, OpenFile *fd,
                size_t pgoffset) {
  if (!(flags & MAP_PRIVATE)) {
    debugf("[ext4::mmap] Unsupported flags! flags{%x}\n", flags);
  }

  uint64_t mappingFlags = PF_USER;
  if (prot & PROT_WRITE)
    mappingFlags |= PF_RW;

  int    pages = DivRoundUp(length, PAGE_SIZE);
  size_t virt = 0;
  if (!(flags & MAP_FIXED)) {
    spinlockAcquire(&currentTask->infoPd->LOCK_PD);
    virt = currentTask->infoPd->mmap_end;
    currentTask->infoPd->mmap_end += pages * PAGE_SIZE;
    spinlockRelease(&currentTask->infoPd->LOCK_PD);
  } else {
    virt = addr;
    if (virt > bootloader.hhdmOffset &&
        virt < (bootloader.hhdmOffset + bootloader.mmTotal))
      return ERR(EACCES);
    else if (virt > bootloader.kernelVirtBase &&
             virt < bootloader.kernelVirtBase + 268435456)
      return ERR(EACCES);
  }

  spinlockAcquire(&currentTask->infoPd->LOCK_PD);
  size_t end = virt + pages * PAGE_SIZE;
  if (end > currentTask->infoPd->mmap_end)
    currentTask->infoPd->mmap_end = end;
  spinlockRelease(&currentTask->infoPd->LOCK_PD);

  // allocate physical space required
  size_t phys = PhysicalAllocate(pages);
  size_t hhdmAddition = bootloader.hhdmOffset + phys;

  // now access it properly (via the HHDM, obviously)
  for (int i = 0; i < pages; i++)
    VirtualMap(virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, mappingFlags);
  memset((void *)(hhdmAddition), 0, pages * PAGE_SIZE);

  // do the read
  Ext4OpenFd *dir = EXT4_DIR_PTR(fd->dir);
  size_t      oldPtr = dir->ptr;
  dir->ptr = pgoffset;
  ext4Read(fd, (void *)hhdmAddition, length);
  dir->ptr = oldPtr;

  return virt;
}

size_t ext4Delete(MountPoint *mnt, char *filename, bool directory,
                  char **symlinkResolve) {
  Ext4FoundObject *global = 0;

  size_t   ret = 0;
  Ext4    *ext4 = EXT4_PTR(mnt->fsInfo);
  uint32_t inodeNum =
      ext4TraversePath(ext4, filename, &global, false, symlinkResolve);
  Ext4Inode *inode = 0;

  spinlockCntWriteAcquire(&ext4->WLOCK_GLOBAL_NOFD);
  if (!inodeNum) {
    ret = ERR(ENOENT);
    goto cleanup;
  }

  atomic_fetch_sub(&global->openFds, 1); // passing &obj increases it

  if (global && atomic_load(&global->openFds)) {
    assert(!directory);
    int   len = strlength(filename) + 1;
    char *filenameToBeDeleted = malloc(len);
    memcpy(filenameToBeDeleted, filename, len);
    atomic_store(&global->filenameToBeDeleted, filenameToBeDeleted);
    ret = 0;
    goto cleanup;
  }

  inode = ext4InodeFetch(ext4, inodeNum);
  if (!inode) {
    ret = ERR(ENOENT);
    goto cleanup;
  }

  if (directory) {
    // we're in directory mode, check if it's a directory
    if (!(inode->permission & S_IFDIR)) {
      ret = ERR(ENOTDIR);
      goto cleanup;
    }
  } else {
    // we're in file mode, check if it's a file
    if (inode->permission & S_IFDIR) {
      ret = ERR(EISDIR);
      goto cleanup;
    }
  }

  // start the fun
  int filenameLen = strlength(filename);
  if (filenameLen == 1) {
    // talking about /
    if (directory)
      ret = ERR(ENOTEMPTY);
    else
      ret = ERR(EISDIR);
    goto cleanup;
  }
  int parentLen = 0;

  if (directory) {
    // directory special: check if the directory is empty first
    uint8_t          *names = (uint8_t *)malloc(ext4->blockSize);
    Ext4Directory    *dir = (Ext4Directory *)names;
    Ext4LookupControl control = {0};
    ext4BlockFetchInit(ext4, &control);
    uint64_t first = ext4BlockFetch(ext4, inode, inodeNum, &control, 0);
    getDiskBytes((uint8_t *)dir, EXT4_BLOCK_TO_LBA(ext4, 0, first),
                 ext4->blockSize / SECTOR_SIZE);
    ext4BlockFetchCleanup(&control);
    int i = 0;
    while (((size_t)dir - (size_t)names) < ext4->blockSize) {
      if (dir->filenameLength > 2 || i > 1) {
        free(names);
        ret = ERR(ENOTEMPTY);
        goto cleanup;
      }
      if (dir->inode)
        i++;
      dir = (void *)((size_t)dir + dir->size);
    }
    free(names);
  }

  // find the parent
  char *parent = (char *)malloc(filenameLen + 1);
  memcpy(parent, filename, filenameLen + 1);
  for (int i = filenameLen; i >= 0; i--) {
    if (parent[i] == '/') {
      if (i != 0)
        parent[i] = '\0';
      parentLen = i;
      break;
    }
    parent[i] = '\0';
  }

  Ext4FoundObject *parentObj = 0;

  // shouldn't need symlinkResolve now since the upper one resolved
  uint32_t parentInodeNum =
      ext4TraversePath(ext4, parent, &parentObj, false, 0);
  assert(parentInodeNum && parentObj);

  Ext4Inode *parentInode = ext4InodeFetch(ext4, parentInodeNum);
  assert(parentInode && parentInode->permission & S_IFDIR);

  ext4JournalBegin(ext4);

  inode->hard_links--;
  if (inode->permission & S_IFDIR &&
      inode->hard_links == 1) { // only the "." thing
    // if we reached this point of the code, it shall be empty
    inode->hard_links = 0;
  }
  if (!inode->hard_links) {
    if (inode->permission & S_IFREG || inode->permission & S_IFDIR) {
      // regular file, delete the contents (really just mark them as free)
      // same applies with empty directories that host the "." & ".." stuff
      Ext4LookupControl control = {0};
      ext4BlockFetchInit(ext4, &control);
      size_t i = 0;
      size_t blocksTakenUp = DivRoundUp(
          COMBINE_64(inode->size_high, inode->size), ext4->blockSize);
      while (true) {
        uint64_t block = ext4BlockFetch(ext4, inode, inodeNum, &control, i++);
        if (!block || i >= blocksTakenUp)
          break;

        uint32_t group = block / ext4->superblock.blocks_per_group;
        uint32_t index = block % ext4->superblock.blocks_per_group;
        ext4BlockDelete(ext4, group, index);
        // todo: free the extent tree blocks themselves
      }
      ext4BlockFetchCleanup(&control);
    }

    // before deleting, do some sanity stuff
    inode->dtime = timerBootUnix + timerTicks / 1000; // needed
    memset(inode->block, 0, sizeof(inode->block));
    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)inode->block;
    hdr->magic = EXT4_EXTENT_MAGIC;
    hdr->entries = 0;
    hdr->max = EXT4_INLINE_EXTENTS_MAX;
    hdr->depth = 0;
    hdr->generation = 0;
    inode->num_sectors = 0;
    inode->blocks_high = 0;
    inode->size = 0;
    inode->size_high = 0;
    ext4InodeModifyM(ext4, inodeNum, inode);

    // get rid of this inode
    ext4InodeDelete(ext4, inodeNum);

    // if it's a directory inform the parent
    if (inode->permission & S_IFDIR) {
      parentInode->hard_links--;
      ext4InodeModifyM(ext4, parentInodeNum, parentInode);
    }
  } else {
    ext4InodeModifyM(ext4, inodeNum, inode);
  }

  // keep only the last part of filename, let's recycle parent
  memmove(parent, &filename[parentLen + 1], filenameLen - parentLen - 1);
  ret = ext4DirRemove(ext4, parentInode, parentInodeNum, parent,
                      filenameLen - parentLen - 1)
            ? 0
            : -1;
  free(parent);

  ext4JournalCommit(ext4);

  // remove the global object
  spinlockAcquire(&ext4->LOCK_OBJECT);
  free(global->filename);
  assert(LinkedListRemove(&parentObj->inner, sizeof(Ext4FoundObject), global));
  spinlockRelease(&ext4->LOCK_OBJECT);

cleanup:
  if (inode)
    free(inode);
  spinlockCntWriteRelease(&ext4->WLOCK_GLOBAL_NOFD);
  return ret;
}

// todo: add nofd write locks here as well & do the mountPoint1 != mountPoint2
// for global move operations/renames!
size_t ext4Link(MountPoint *mnt, char *filename, char *target,
                char **symlinkResolve, char **symlinkResolveTarget) {
  Ext4    *ext4 = EXT4_PTR(mnt->fsInfo);
  uint32_t inodeNum =
      ext4TraversePath(ext4, filename, 0, false, symlinkResolve);
  if (!inodeNum)
    return ERR(ENOENT);

  Ext4Inode *inode = ext4InodeFetch(ext4, inodeNum);
  if (!inode)
    return ERR(ENOENT);
  if (!(inode->permission & S_IFREG || inode->permission & S_IFDIR)) {
    free(inode);
    return ERR(EPERM);
  }

  char *targetDir = strdup(target);
  char *targetFilename = strrchr(targetDir, '/');
  if (!targetFilename) {
    free(inode);
    free(targetDir);
    return ERR(ENOENT);
  }
  *targetFilename = '\0'; // zero so targetDir works
  targetFilename++;       // targetFilename starts afterwards

  uint32_t targetDirInodeNum =
      ext4TraversePath(ext4, targetDir, 0, false, symlinkResolveTarget);
  if (!targetDirInodeNum) {
    free(inode);
    free(targetDir);
    return ERR(ENOENT);
  }

  Ext4Inode *targetDirInode = ext4InodeFetch(ext4, targetDirInodeNum);
  assert(targetDirInode->permission & S_IFDIR); // not checking again

  ext4JournalBegin(ext4);

  // make it hard
  inode->hard_links++;
  ext4InodeModifyM(ext4, inodeNum, inode);

  // create a directory entry
  uint8_t dirType =
      inode->permission & S_IFREG ? EXT4_FT_REG_FILE : EXT4_FT_DIR;
  ext4DirAllocate(ext4, targetDirInodeNum, targetDirInode, targetFilename,
                  strlength(targetFilename), dirType, inodeNum);

  ext4JournalCommit(ext4);

  // cleanup
  free(inode);
  free(targetDirInode);
  free(targetDir);
  return 0;
}

VfsHandlers ext4Handlers = {.open = ext4Open,
                            .write = ext4Write,
                            .close = ext4Close,
                            .duplicate = ext4DuplicateNodeUnsafe,
                            .read = ext4Read,
                            .stat = ext4StatFd,
                            .getdents64 = ext4Getdents64,
                            .seek = ext4Seek,
                            .getFilesize = ext4GetFilesize,
                            .mmap = ext4Mmap};