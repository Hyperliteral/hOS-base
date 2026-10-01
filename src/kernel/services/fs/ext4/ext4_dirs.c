#include <dents.h>
#include <ext4.h>
#include <malloc.h>
#include <system.h>
#include <timer.h>
#include <util.h>

bool ext4DirAllocate(Ext4 *ext4, uint32_t inodeNum, Ext4Inode *parentDirInode,
                     char *filename, uint8_t filenameLen, uint8_t type,
                     uint32_t inode) {
  spinlockAcquire(&ext4->LOCK_DIRALLOC);
  int entryLen = sizeof(Ext4Directory) + filenameLen;

  Ext4Inode *ino = parentDirInode; // <- todo
  uint8_t   *names = (uint8_t *)malloc(ext4->blockSize);

  Ext4LookupControl control = {0};
  size_t            blockNum = 0;

  bool ret = false;
  ext4BlockFetchInit(ext4, &control);

  int blocksContained =
      DivRoundUp(COMBINE_64(ino->size_high, ino->size), ext4->blockSize);
  for (int i = 0; i < blocksContained; i++) {
    uint64_t block = ext4BlockFetch(ext4, ino, inodeNum, &control, blockNum);
    if (!block)
      break;
    blockNum++;
    Ext4Directory *dir = (Ext4Directory *)names;

    getDiskBytes(names, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);

    while (((size_t)dir - (size_t)names) < ext4->blockSize) {
      if (dir->inode && filenameLen == dir->filenameLength &&
          memcmp(dir->filename, filename, filenameLen) == 0) {
        ret = false;
        goto cleanup;
      }
      int minForOld = (sizeof(Ext4Directory) + dir->filenameLength + 3) & ~3;
      int minForNew = (entryLen + 3) & ~3;

      int remainderForNew = dir->size - minForOld;
      if (remainderForNew < minForNew)
        goto next_item;

      // means we now have enough space to put the new one in

      dir->size = minForOld;
      Ext4Directory *new = (void *)((size_t)dir + dir->size);
      new->size = remainderForNew;

      new->type = type;
      memcpy(new->filename, filename, filenameLen);
      new->filenameLength = filenameLen;
      new->inode = inode;

      ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, block), names,
                        ext4->blockSize / SECTOR_SIZE);

      ret = true;
      goto cleanup;

    next_item:
      dir = (void *)((size_t)dir + dir->size);
    }
  }

  // means we need to allocate another block for these
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inodeNum);
  uint32_t newBlock = ext4BlockFind(ext4, group, 1);

  uint8_t *newBlockBuff = names; // reuse names :p
  getDiskBytes(newBlockBuff, EXT4_BLOCK_TO_LBA(ext4, 0, newBlock),
               ext4->blockSize / SECTOR_SIZE);

  Ext4Directory *new = (Ext4Directory *)(newBlockBuff);
  new->size = ext4->blockSize;
  new->type = type;
  memcpy(new->filename, filename, filenameLen);
  new->filenameLength = filenameLen;
  new->inode = inode;

  ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, newBlock), newBlockBuff,
                    ext4->blockSize / SECTOR_SIZE);
  ext4ExtentInsertBlock(ext4, ino, inodeNum, &control, blockNum, newBlock);

  ino->size += ext4->blockSize;
  ino->num_sectors = ext4BlockSizeCalculate(ext4, ino->size) / SECTOR_SIZE;
  ext4InodeModifyM(ext4, inodeNum, ino);

  ret = true;

cleanup:
  ext4BlockFetchCleanup(&control);
  free(names);
  spinlockRelease(&ext4->LOCK_DIRALLOC);

  return ret;
}

bool ext4DirRemove(Ext4 *ext4, Ext4Inode *parentDirInode,
                   uint32_t parentDirInodeNum, char *filename,
                   uint8_t filenameLen) {
  spinlockAcquire(&ext4->LOCK_DIRALLOC);

  Ext4Inode *ino = parentDirInode; // <- todo
  uint8_t   *names = (uint8_t *)malloc(ext4->blockSize);

  Ext4LookupControl control = {0};
  size_t            blockNum = 0;

  bool ret = false;
  ext4BlockFetchInit(ext4, &control);

  int blocksContained =
      DivRoundUp(COMBINE_64(ino->size_high, ino->size), ext4->blockSize);
  for (int i = 0; i < blocksContained; i++) {
    uint64_t block =
        ext4BlockFetch(ext4, ino, parentDirInodeNum, &control, blockNum);
    if (!block)
      break;
    blockNum++;
    Ext4Directory *dir = (Ext4Directory *)names;

    getDiskBytes(names, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);

    Ext4Directory *before = 0;
    while (((size_t)dir - (size_t)names) < ext4->blockSize) {
      if (dir->inode && filenameLen == dir->filenameLength &&
          memcmp(dir->filename, filename, filenameLen) == 0) {
        // found!
        if ((size_t)dir == (size_t)names) {
          // it's the first element
          assert(!before);
          dir->inode = 0;
          dir->filenameLength = 0;
          ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, block), names,
                            ext4->blockSize / SECTOR_SIZE);
        } else {
          // it's somewhere in between, meaning there's another element behind
          before->size += dir->size;
          ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, block), names,
                            ext4->blockSize / SECTOR_SIZE);
        }
        // done successfuly!
        ret = true;
      }

      before = dir;
      dir = (void *)((size_t)dir + dir->size);
    }
  }

  ext4BlockFetchCleanup(&control);
  free(names);
  spinlockRelease(&ext4->LOCK_DIRALLOC);

  return ret;
}

size_t ext4Getdents64(OpenFile *file, struct linux_dirent64 *start,
                      unsigned int hardlimit) {
  Ext4       *ext4 = EXT4_PTR(file->mountPoint->fsInfo);
  Ext4OpenFd *edir = EXT4_DIR_PTR(file->dir);

  if ((edir->inode.permission & 0xF000) != EXT4_S_IFDIR)
    return ERR(ENOTDIR);

  size_t     allocatedlimit = 0;
  Ext4Inode *ino = &edir->inode;
  uint8_t   *names = (uint8_t *)malloc(ext4->blockSize);

  struct linux_dirent64 *dirp = (struct linux_dirent64 *)start;

  int blocksContained =
      DivRoundUp(COMBINE_64(ino->size_high, ino->size), ext4->blockSize);
  for (int i = 0; i < blocksContained; i++) {
    uint64_t block = ext4BlockFetch(ext4, ino, edir->inodeNum, &edir->lookup,
                                    edir->ptr / ext4->blockSize);
    if (!block)
      break;
    Ext4Directory *dir =
        (Ext4Directory *)((size_t)names + (edir->ptr % ext4->blockSize));

    getDiskBytes(names, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);

    while (((size_t)dir - (size_t)names) < ext4->blockSize) {
      if (!dir->inode) {
        goto traverse;
        continue;
      }

      unsigned char type = 0;
      if (dir->type == 2)
        type = CDT_DIR;
      else if (dir->type == 7)
        type = CDT_LNK;
      else
        type = CDT_REG;

      DENTS_RES res =
          dentsAdd(start, &dirp, &allocatedlimit, hardlimit, dir->filename,
                   dir->filenameLength, dir->inode, type);

      if (res == DENTS_NO_SPACE) {
        allocatedlimit = ERR(EINVAL);
        goto cleanup;
      } else if (res == DENTS_RETURN)
        goto cleanup;

      edir->ptr += dir->size;

    traverse:
      dir = (void *)((size_t)dir + dir->size);
    }

    int rem = edir->ptr % ext4->blockSize;
    if (rem)
      edir->ptr += ext4->blockSize - rem;
  }

cleanup:
  free(names);
  return allocatedlimit;
}