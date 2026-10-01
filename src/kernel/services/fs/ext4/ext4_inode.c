#include <ext4.h>
#include <malloc.h>
#include <system.h>
#include <util.h>

Ext4Inode *ext4InodeFetch(Ext4 *ext4, size_t inode) {
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inode);
  uint32_t index = EXT4_INODE_TO_INDEX(ext4, inode);

  spinlockCntReadAcquire(&ext4->WLOCKS_INODE[group]);

  size_t leftovers = index * ext4->inodeSize;
  size_t leftoversLba = leftovers / SECTOR_SIZE;
  size_t leftoversRem = leftovers % SECTOR_SIZE;

  // large enough just in case
  size_t len = DivRoundUp(ext4->inodeSize * 4, SECTOR_SIZE) * SECTOR_SIZE;
  size_t lba =
      EXT4_BLOCK_TO_LBA(ext4, 0, ext4BgInodeTable(ext4, group)) + leftoversLba;

  uint8_t *buf = (uint8_t *)malloc(len);
  getDiskBytes(buf, lba, len / SECTOR_SIZE);
  Ext4Inode *tmp = (Ext4Inode *)(buf + leftoversRem);

  Ext4Inode *ret = (Ext4Inode *)malloc(ext4->inodeSize);
  memcpy(ret, tmp, ext4->inodeSize);

  free(buf);
  spinlockCntReadRelease(&ext4->WLOCKS_INODE[group]);
  return ret;
}

// IMPORTANT! Remember to manually set the lock **before** calling
void ext4InodeModifyM(Ext4 *ext4, size_t inode, Ext4Inode *target) {
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inode);
  uint32_t index = EXT4_INODE_TO_INDEX(ext4, inode);

  spinlockCntWriteAcquire(&ext4->WLOCKS_INODE[group]);

  size_t leftovers = index * ext4->inodeSize;
  size_t leftoversLba = leftovers / SECTOR_SIZE;
  size_t leftoversRem = leftovers % SECTOR_SIZE;

  // large enough just in case
  size_t len = DivRoundUp(ext4->inodeSize * 4, SECTOR_SIZE) * SECTOR_SIZE;
  size_t lba =
      EXT4_BLOCK_TO_LBA(ext4, 0, ext4BgInodeTable(ext4, group)) + leftoversLba;

  uint8_t *buf = (uint8_t *)malloc(len);
  getDiskBytes(buf, lba, len / SECTOR_SIZE);
  Ext4Inode *tmp = (Ext4Inode *)(buf + leftoversRem);
  memcpy(tmp, target, sizeof(Ext4Inode));
  ext4StageMetadata(ext4, lba, buf, len / SECTOR_SIZE);

  free(buf);
  spinlockCntWriteRelease(&ext4->WLOCKS_INODE[group]);
}

void ext4InodeDelete(Ext4 *ext4, size_t inode) {
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inode);
  uint32_t index = EXT4_INODE_TO_INDEX(ext4, inode);

  spinlockCntWriteAcquire(&ext4->WLOCKS_INODE[group]);

  uint32_t where = index / 8;
  uint32_t remainder = index % 8;

  size_t lba = EXT4_BLOCK_TO_LBA(ext4, 0, ext4BgInodeBitmap(ext4, group));

  uint8_t *buf = (uint8_t *)malloc(ext4->blockSize);
  getDiskBytes(buf, lba, ext4->blockSize / SECTOR_SIZE);
  assert(buf[where] & (1 << remainder));
  buf[where] &= ~(1 << remainder);
  ext4StageMetadata(ext4, lba, buf, ext4->blockSize / SECTOR_SIZE);

  free(buf);

  // set the bgdt accordingly
  spinlockAcquire(&ext4->LOCK_BGDT_WRITE);
  *(uint16_t *)(ext4->bgdts + group * ext4->blockGD + 0x0e) += 1;
  ext4BgdtPushM(ext4);
  spinlockRelease(&ext4->LOCK_BGDT_WRITE);

  // and the superblock
  spinlockAcquire(&ext4->LOCK_SUPERBLOCK_WRITE);
  ext4->superblock.free_inodes_count += 1;
  ext4SuperblockPushM(ext4);
  spinlockRelease(&ext4->LOCK_SUPERBLOCK_WRITE);

  spinlockCntWriteRelease(&ext4->WLOCKS_INODE[group]);
}

uint32_t ext4InodeFind(Ext4 *ext4, int groupSuggestion) {
  if (ext4->superblock.free_inodes_count < 1)
    goto burn;

  uint32_t suggested = ext4InodeFindL(ext4, groupSuggestion);
  if (suggested)
    return suggested;

  for (int i = 0; i < ext4->blockGroups; i++) {
    if (i == groupSuggestion)
      continue;

    uint32_t ret = ext4InodeFindL(ext4, i);
    if (ret)
      return ret;
  }

burn:
  debugf("[ext4] FATAL! Couldn't find a single inode! Drive is full!\n");
  panic();
  return 0;
}

uint32_t ext4InodeFindL(Ext4 *ext4, int group) {
  if (ext4BgFreeInodes(ext4, group) < 1)
    return 0;

  spinlockCntWriteAcquire(&ext4->WLOCKS_INODE[group]);

  uint32_t ret = 0;
  uint8_t *buff = malloc(ext4->blockSize);

  getDiskBytes(buff, EXT4_BLOCK_TO_LBA(ext4, 0, ext4BgInodeBitmap(ext4, group)),
               ext4->blockSize / SECTOR_SIZE);

  int firstInodeDiv = 0;
  int firstInodeRem = 0;

  if (group == 0) {
    firstInodeDiv = ext4->superblock.first_ino / 8;
    firstInodeRem = ext4->superblock.first_ino % 8;
  }

  for (int i = firstInodeDiv; i < ext4->blockSize; i++) {
    if (buff[i] == 0xff)
      continue;
    for (int j = (i == firstInodeDiv ? firstInodeRem : 0); j < 8; j++) {
      if (!(buff[i] & (1 << j))) {
        ret = i * 8 + j;
        goto cleanup;
      }
    }
  }

cleanup:
  if (ret) {
    // we found inodes successfully, mark them as allocated
    uint32_t where = ret / 8;
    uint32_t remainder = ret % 8;
    buff[where] |= (1 << remainder);
    ext4StageMetadata(
        ext4, EXT4_BLOCK_TO_LBA(ext4, 0, ext4BgInodeBitmap(ext4, group)), buff,
        ext4->blockSize / SECTOR_SIZE);

    // set the bgdt accordingly
    spinlockAcquire(&ext4->LOCK_BGDT_WRITE);
    *(uint16_t *)(ext4->bgdts + group * ext4->blockGD + 0x0e) -= 1;
    ext4BgdtPushM(ext4);
    spinlockRelease(&ext4->LOCK_BGDT_WRITE);

    // and the superblock
    spinlockAcquire(&ext4->LOCK_SUPERBLOCK_WRITE);
    ext4->superblock.free_inodes_count -= 1;
    ext4SuperblockPushM(ext4);
    spinlockRelease(&ext4->LOCK_SUPERBLOCK_WRITE);
  }

  spinlockCntWriteRelease(&ext4->WLOCKS_INODE[group]);
  // +1 necessary because inodes start at inode number 1
  return ret ? (group * ext4->superblock.inodes_per_group + ret + 1) : 0;
}