#include <ext4.h>
#include <malloc.h>
#include <system.h>
#include <util.h>

static inline uint64_t ext4BgField32(Ext4 *ext4, size_t i, size_t off) {
  return *(uint32_t *)(ext4->bgdts + i * ext4->blockGD + off);
}

static inline uint16_t ext4BgField16(Ext4 *ext4, size_t i, size_t off) {
  return *(uint16_t *)(ext4->bgdts + i * ext4->blockGD + off);
}

uint64_t ext4BgBlockBitmap(Ext4 *ext4, size_t i) {
  uint64_t ret = ext4BgField32(ext4, i, 0x00);
  if (ext4->bgdt64)
    ret |= (uint64_t)ext4BgField32(ext4, i, 0x24) << 32;
  return ret;
}

uint64_t ext4BgInodeBitmap(Ext4 *ext4, size_t i) {
  uint64_t ret = ext4BgField32(ext4, i, 0x04);
  if (ext4->bgdt64)
    ret |= (uint64_t)ext4BgField32(ext4, i, 0x28) << 32;
  return ret;
}

uint64_t ext4BgInodeTable(Ext4 *ext4, size_t i) {
  uint64_t ret = ext4BgField32(ext4, i, 0x08);
  if (ext4->bgdt64)
    ret |= (uint64_t)ext4BgField32(ext4, i, 0x2c) << 32;
  return ret;
}

uint64_t ext4BgFreeBlocks(Ext4 *ext4, size_t i) {
  uint64_t ret = ext4BgField16(ext4, i, 0x0c);
  if (ext4->bgdt64)
    ret |= (uint64_t)ext4BgField16(ext4, i, 0x30) << 16;
  return ret;
}

uint64_t ext4BgFreeInodes(Ext4 *ext4, size_t i) {
  uint64_t ret = ext4BgField16(ext4, i, 0x0e);
  if (ext4->bgdt64)
    ret |= (uint64_t)ext4BgField16(ext4, i, 0x32) << 16;
  return ret;
}

uint64_t ext4SuperBlockCount(Ext4 *ext4) {
  uint64_t ret = ext4->superblock.blocks_count_lo;
  if (ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_64BIT)
    ret |= (uint64_t)ext4->superblock.blocks_count_hi << 32;
  return ret;
}

uint64_t ext4SuperFreeBlocks(Ext4 *ext4) {
  uint64_t ret = ext4->superblock.free_blocks_count_lo;
  if (ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_64BIT)
    ret |= (uint64_t)ext4->superblock.free_blocks_count_hi << 32;
  return ret;
}

void ext4BlockFetchInit(Ext4 *ext4, Ext4LookupControl *control) {
  control->tmp1 = (uint32_t *)malloc(ext4->blockSize);
}

void ext4BlockFetchCleanup(Ext4LookupControl *control) {
  if (control->tmp1)
    free(control->tmp1);
  control->tmp1Block = 0;
}

size_t ext4BlockSizeCalculate(Ext4 *ext4, size_t raw) {
  uint32_t blocks = DivRoundUp(raw, ext4->blockSize);
  return blocks * ext4->blockSize;
}

uint64_t ext4BlockFetch(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                        Ext4LookupControl *control, size_t curr) {
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inodeNum);
  spinlockCntReadAcquire(&ext4->WLOCKS_BLOCK_BITMAP[group]);

  uint64_t ret = 0;
  if (!(ino->flags & EXT4_EXTENTS_FL))
    goto cleanup;

  Ext4ExtentHeader *root = (Ext4ExtentHeader *)ino->block;
  if (root->magic != EXT4_EXTENT_MAGIC)
    goto cleanup;

  if (root->depth == 0) {
    Ext4Extent *e = (Ext4Extent *)(ino->block + sizeof(Ext4ExtentHeader));
    for (int i = 0; i < root->entries; i++) {
      if (curr >= e[i].ee_block && curr < (size_t)e[i].ee_block + e[i].ee_len) {
        uint64_t start = ((uint64_t)e[i].ee_start_hi) << 32 | e[i].ee_start_lo;
        ret = start + (curr - e[i].ee_block);
        goto cleanup;
      }
    }
    goto cleanup;
  }

  uint8_t *node = (uint8_t *)ino->block;
  int      depth = root->depth;
  while (depth > 0) {
    Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)node;
    if (hdr->magic != EXT4_EXTENT_MAGIC)
      goto cleanup;

    Ext4ExtentIndex *idx =
        (Ext4ExtentIndex *)((uint8_t *)node + sizeof(Ext4ExtentHeader));
    int chosen = -1;
    for (int i = 0; i < hdr->entries; i++) {
      if (idx[i].ei_block <= curr)
        chosen = i;
      else
        break;
    }
    if (chosen < 0)
      goto cleanup;

    uint64_t child =
        ((uint64_t)idx[chosen].ei_leaf_hi) << 32 | idx[chosen].ei_leaf_lo;
    depth--;

    size_t lba = EXT4_BLOCK_TO_LBA(ext4, 0, child);
    if (control->tmp1Block != lba) {
      getDiskBytes((uint8_t *)control->tmp1, lba,
                   ext4->blockSize / SECTOR_SIZE);
      control->tmp1Block = lba;
    }

    if (depth == 0) {
      // leaf block, scan extents
      Ext4ExtentHeader *lhdr = (Ext4ExtentHeader *)control->tmp1;
      if (lhdr->magic != EXT4_EXTENT_MAGIC)
        goto cleanup;
      Ext4Extent *e =
          (Ext4Extent *)((uint8_t *)control->tmp1 + sizeof(Ext4ExtentHeader));
      for (int i = 0; i < lhdr->entries; i++) {
        if (curr >= e[i].ee_block &&
            curr < (size_t)e[i].ee_block + e[i].ee_len) {
          uint64_t start =
              ((uint64_t)e[i].ee_start_hi) << 32 | e[i].ee_start_lo;
          ret = start + (curr - e[i].ee_block);
          goto cleanup;
        }
      }
      goto cleanup;
    }

    node = (uint8_t *)control->tmp1;
  }

cleanup:
  spinlockCntReadRelease(&ext4->WLOCKS_BLOCK_BITMAP[group]);
  return ret;
}

uint64_t *ext4BlockChain(Ext4 *ext4, Ext4OpenFd *fd, size_t curr,
                         size_t blocks) {
  uint64_t *ret = (uint64_t *)malloc((1 + blocks) * sizeof(uint64_t));
  for (int i = 0; i < (1 + blocks); i++) // will take care of curr too
  {
    ret[i] = ext4BlockFetch(ext4, &fd->inode, fd->inodeNum, &fd->lookup, curr);
    curr++;
  }
  return ret;
}

// ---- extent tree flattening / merging / committing ----

static bool ext4FlattenNode(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                            Ext4LookupControl *control, uint8_t *nodeView,
                            bool inlineRoot, Ext4Extent *list, int listCap,
                            int *count) {
  (void)inodeNum;
  Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)nodeView;
  if (hdr->magic != EXT4_EXTENT_MAGIC)
    return false;

  if (hdr->depth == 0) {
    Ext4Extent *e =
        (Ext4Extent *)(inlineRoot
                           ? (uint8_t *)ino->block + sizeof(Ext4ExtentHeader)
                           : nodeView + sizeof(Ext4ExtentHeader));
    for (int i = 0; i < hdr->entries; i++) {
      if (*count >= listCap)
        return false;
      list[(*count)++] = e[i];
    }
    return true;
  }

  size_t nodeCap = inlineRoot ? EXT4_INLINE_EXTENTS_MAX
                              : (ext4->blockSize - sizeof(Ext4ExtentHeader)) /
                                    sizeof(Ext4ExtentIndex);
  size_t n = MIN(hdr->entries, nodeCap);
  if (!n)
    return true;

  // snapshot indices: we share control->tmp1 for every child load
  Ext4ExtentIndex *idxCopy =
      (Ext4ExtentIndex *)malloc(n * sizeof(Ext4ExtentIndex));
  Ext4ExtentIndex *idx =
      (Ext4ExtentIndex *)(nodeView + sizeof(Ext4ExtentHeader));
  memcpy(idxCopy, idx, n * sizeof(Ext4ExtentIndex));

  for (size_t i = 0; i < n; i++) {
    uint64_t child =
        ((uint64_t)idxCopy[i].ei_leaf_hi) << 32 | idxCopy[i].ei_leaf_lo;
    size_t lba = EXT4_BLOCK_TO_LBA(ext4, 0, child);
    getDiskBytes((uint8_t *)control->tmp1, lba, ext4->blockSize / SECTOR_SIZE);
    if (!ext4FlattenNode(ext4, ino, inodeNum, control, (uint8_t *)control->tmp1,
                         false, list, listCap, count)) {
      free(idxCopy);
      return false;
    }
  }

  free(idxCopy);
  return true;
}

int ext4ExtentFlatten(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                      Ext4LookupControl *control, Ext4Extent **out) {
  *out = 0;
  if (!(ino->flags & EXT4_EXTENTS_FL))
    return 0;

  Ext4ExtentHeader *root = (Ext4ExtentHeader *)ino->block;
  if (root->magic != EXT4_EXTENT_MAGIC)
    return 0;

  int         cap = 64;
  Ext4Extent *list = (Ext4Extent *)malloc(cap * sizeof(Ext4Extent));
  int         count = 0;
  if (!ext4FlattenNode(ext4, ino, inodeNum, control, (uint8_t *)ino->block,
                       true, list, cap, &count)) {
    free(list);
    return -1;
  }

  *out = list;
  return count;
}

static void ext4MergeSort(Ext4Extent *a, int lo, int hi, Ext4Extent *tmp) {
  if (hi - lo <= 1)
    return;

  int mid = (lo + hi) / 2;
  ext4MergeSort(a, lo, mid, tmp);
  ext4MergeSort(a, mid, hi, tmp);

  int i = lo;
  int j = mid;
  int k = lo;
  while (i < mid && j < hi) {
    if (a[i].ee_block <= a[j].ee_block)
      tmp[k++] = a[i++];
    else
      tmp[k++] = a[j++];
  }
  while (i < mid)
    tmp[k++] = a[i++];
  while (j < hi)
    tmp[k++] = a[j++];

  for (i = lo; i < hi; i++)
    a[i] = tmp[i];
}

static bool ext4CanMerge(Ext4Extent *a, Ext4Extent *b) {
  uint64_t aStart = ((uint64_t)a->ee_start_hi) << 32 | a->ee_start_lo;
  uint64_t bStart = ((uint64_t)b->ee_start_hi) << 32 | b->ee_start_lo;
  return b->ee_block == (uint32_t)(a->ee_block + a->ee_len) &&
         bStart == aStart + a->ee_len && (a->ee_len + b->ee_len) <= 32768;
}

static Ext4Extent ext4ExtentSpan(Ext4Extent *a, Ext4Extent *b) {
  Ext4Extent ret = *a;
  ret.ee_len = a->ee_len + b->ee_len;
  return ret;
}

static int ext4SortAndMergeInternal(Ext4Extent *list, int count) {
  if (count < 2)
    return count;

  Ext4Extent *tmp = (Ext4Extent *)malloc(count * sizeof(Ext4Extent));
  ext4MergeSort(list, 0, count, tmp);
  free(tmp);

  int out = 0;
  for (int i = 1; i < count; i++) {
    if (ext4CanMerge(&list[out], &list[i]))
      list[out] = ext4ExtentSpan(&list[out], &list[i]);
    else
      list[++out] = list[i];
  }
  return out + 1;
}

int ext4ExtentMerge(Ext4Extent *list, int count) {
  return ext4SortAndMergeInternal(list, count);
}

static uint32_t ext4ExtentLeafCap(Ext4 *ext4) {
  return (ext4->blockSize - sizeof(Ext4ExtentHeader)) / sizeof(Ext4Extent);
}

// builds a tree for [list, count) rooted at `buffer`. isRoot uses the inline
// i_block area, everything else is a freshly allocated block that gets written
// to disk before returning (returns its physical block number)
static uint64_t ext4CommitBuildNode(Ext4 *ext4, uint32_t inodeNum,
                                    Ext4Extent *list, int count, int depth,
                                    bool isRoot, uint64_t *blocksUsed) {
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inodeNum);
  uint32_t nodeMax = isRoot ? EXT4_INLINE_EXTENTS_MAX : ext4ExtentLeafCap(ext4);

  uint8_t *buff;
  uint64_t self = 0;
  if (isRoot) {
    buff = (uint8_t *)ext4->rootScratchInode; // caller sets this to ino->block
  } else {
    self = ext4BlockFind(ext4, group, 1);
    (*blocksUsed)++;
    buff = (uint8_t *)malloc(ext4->blockSize);
  }

  memset(buff, 0, isRoot ? 60 : ext4->blockSize);

  Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)buff;
  hdr->magic = EXT4_EXTENT_MAGIC;
  hdr->max = nodeMax;
  hdr->depth = depth;

  if (depth == 0) {
    hdr->entries = count;
    memcpy(buff + sizeof(Ext4ExtentHeader), list, count * sizeof(Ext4Extent));
  } else {
    size_t   childrenMax = isRoot ? EXT4_INLINE_EXTENTS_MAX : nodeMax;
    size_t   perChild = DivRoundUp(count, childrenMax);
    int      pos = 0;
    int      entries = 0;
    uint8_t *idxArea = buff + sizeof(Ext4ExtentHeader);
    int      idxSize = 0;
    for (size_t i = 0; i < childrenMax && pos < count; i++) {
      int take = MIN((int)perChild, count - pos);

      uint64_t child;
      if (depth == 1) {
        if (take > ext4ExtentLeafCap(ext4)) {
          // guard against overflowing a leaf block
          if (!isRoot)
            free(buff);
          return 0;
        }
        child = ext4BlockFind(ext4, group, 1);
        (*blocksUsed)++;
        uint8_t *leaf = (uint8_t *)malloc(ext4->blockSize);
        memset(leaf, 0, ext4->blockSize);
        Ext4ExtentHeader *lhdr = (Ext4ExtentHeader *)leaf;
        lhdr->magic = EXT4_EXTENT_MAGIC;
        lhdr->entries = take;
        lhdr->max = ext4ExtentLeafCap(ext4);
        lhdr->depth = 0;
        memcpy(leaf + sizeof(Ext4ExtentHeader), &list[pos],
               take * sizeof(Ext4Extent));
        ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, child), leaf,
                          ext4->blockSize / SECTOR_SIZE);
        free(leaf);
      } else {
        child = ext4CommitBuildNode(ext4, inodeNum, &list[pos], take, depth - 1,
                                    false, blocksUsed);
        if (!child) {
          if (!isRoot)
            free(buff);
          return 0;
        }
      }

      Ext4ExtentIndex *entry = (Ext4ExtentIndex *)(idxArea + idxSize);
      entry->ei_block = list[pos].ee_block;
      entry->ei_leaf_lo = child;
      entry->ei_leaf_hi = child >> 32;
      idxSize += sizeof(Ext4ExtentIndex);
      entries++;
      pos += take;
    }
    hdr->entries = entries;
  }

  if (isRoot) {
    // written back together with the inode by the caller
  } else {
    ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, self), buff,
                      ext4->blockSize / SECTOR_SIZE);
    free(buff);
  }

  return self;
}

void ext4ExtentCommit(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                      Ext4LookupControl *control, Ext4Extent *list, int count,
                      uint64_t *blocksUsedOut) {
  (void)control;
  ext4->rootScratchInode = (uint8_t *)ino->block;

  uint64_t treeBlocks = 0;
  uint32_t leafCap = ext4ExtentLeafCap(ext4);

  if (count <= EXT4_INLINE_EXTENTS_MAX) {
    ext4CommitBuildNode(ext4, inodeNum, list, count, 0, true, &treeBlocks);
  } else {
    // find d: the smallest index depth such that the top-level fanout covers
    // every leaf. root fanout = EXT4_INLINE_EXTENTS_MAX, others = leafCap
    int      d = 1;
    uint64_t leaves = DivRoundUp(count, leafCap);
    uint64_t capacity = EXT4_INLINE_EXTENTS_MAX;
    uint64_t spread = 1;
    while (leaves > capacity) {
      d++;
      spread *= leafCap;
      if (spread > 0x100000000)
        break; // practically unreachable, bail out to a too-shallow tree
      capacity = (uint64_t)EXT4_INLINE_EXTENTS_MAX * spread;
    }
    // grows the root to hold index entries of depth d
    ext4CommitBuildNode(ext4, inodeNum, list, count, d, true, &treeBlocks);
  }

  uint64_t dataBlocks = 0;
  for (int i = 0; i < count; i++)
    dataBlocks += list[i].ee_len;

  uint64_t sectors =
      (dataBlocks + treeBlocks) * (ext4->blockSize / SECTOR_SIZE);
  ino->num_sectors = sectors;         // low 32
  ino->blocks_high = (sectors >> 32); // 32..47
  // our i_blocks is 48 bits, bits above 2^48 are dropped

  if (blocksUsedOut)
    *blocksUsedOut = treeBlocks;
}

void ext4ExtentInsertBlock(Ext4 *ext4, Ext4Inode *ino, uint32_t inodeNum,
                           Ext4LookupControl *control, uint32_t logical,
                           uint64_t physical) {
  Ext4Extent *list = 0;
  int         count = ext4ExtentFlatten(ext4, ino, inodeNum, control, &list);
  if (count < 0) { // corrupt tree, don't scribble over it
    if (list)
      free(list);
    return;
  }

  Ext4Extent *grown = (Ext4Extent *)malloc((count + 1) * sizeof(Ext4Extent));
  if (count)
    memcpy(grown, list, count * sizeof(Ext4Extent));
  if (list)
    free(list);

  grown[count].ee_block = logical;
  grown[count].ee_len = 1;
  grown[count].ee_start_hi = physical >> 32;
  grown[count].ee_start_lo = physical;

  int merged = ext4ExtentMerge(grown, count + 1);
  ext4ExtentCommit(ext4, ino, inodeNum, control, grown, merged, 0);
  free(grown);
}

// ---- allocation ----

uint32_t ext4BlockFind(Ext4 *ext4, int groupSuggestion, uint32_t amnt) {
  if (ext4SuperFreeBlocks(ext4) < amnt)
    goto burn;

  uint32_t suggested = ext4BlockFindL(ext4, groupSuggestion, amnt);
  if (suggested)
    return suggested;

  for (int i = 0; i < ext4->blockGroups; i++) {
    if (i == groupSuggestion)
      continue;

    uint32_t ret = ext4BlockFindL(ext4, i, amnt);
    if (ret)
      return ret;
  }

burn:
  debugf("[ext4] FATAL! Couldn't find blocks! Drive is full! amnt{%d}\n", amnt);
  panic();
  return 0;
}

uint32_t ext4BlockFindL(Ext4 *ext4, int group, uint32_t amnt) {
  if (ext4BgFreeBlocks(ext4, group) < amnt)
    return 0;

  spinlockCntWriteAcquire(&ext4->WLOCKS_BLOCK_BITMAP[group]);

  uint32_t ret = 0;
  uint64_t bitmap = ext4BgBlockBitmap(ext4, group);
  uint8_t *buff = malloc(ext4->blockSize);

  getDiskBytes(buff, EXT4_BLOCK_TO_LBA(ext4, 0, bitmap),
               ext4->blockSize / SECTOR_SIZE);

  uint32_t foundBlk = 0;
  uint32_t foundAmnt = 0;
  for (int i = 0; i < ext4->blockSize; i++) {
    for (int j = 0; j < 8; j++) {
      if (buff[i] & (1 << j)) {
        foundBlk = i * 8 + j + 1; // next one
        foundAmnt = 0;
      } else {
        foundAmnt++;
        if (foundAmnt >= amnt) {
          ret = foundBlk;
          goto cleanup;
        }
      }
    }
  }

cleanup:
  if (ret) {
    // we found blocks successfully, mark them as allocated
    for (int i = 0; i < amnt; i++) {
      uint32_t where = (foundBlk + i) / 8;
      uint32_t remainder = (foundBlk + i) % 8;
      buff[where] |= (1 << remainder);
    }
    ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, bitmap), buff,
                      ext4->blockSize / SECTOR_SIZE);

    // set the bgdt accordingly (a group holds at most 65535 blocks, so the
    // 16-bit low half never borrows on hobby-sized volumes)
    spinlockAcquire(&ext4->LOCK_BGDT_WRITE);
    *(uint16_t *)(ext4->bgdts + group * ext4->blockGD + 0x0c) -= amnt;
    ext4BgdtPushM(ext4);
    spinlockRelease(&ext4->LOCK_BGDT_WRITE);

    // and the superblock
    spinlockAcquire(&ext4->LOCK_SUPERBLOCK_WRITE);
    if (ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_64BIT)
      ext4->superblock.free_blocks_count_hi =
          (ext4SuperFreeBlocks(ext4) - amnt) >> 32;
    ext4->superblock.free_blocks_count_lo -= amnt;
    ext4SuperblockPushM(ext4);
    spinlockRelease(&ext4->LOCK_SUPERBLOCK_WRITE);
  }

  spinlockCntWriteRelease(&ext4->WLOCKS_BLOCK_BITMAP[group]);
  return ret ? (group * ext4->superblock.blocks_per_group + ret) : 0;
}

void ext4BlockDelete(Ext4 *ext4, uint32_t group, uint32_t index) {
  spinlockCntWriteAcquire(&ext4->WLOCKS_BLOCK_BITMAP[group]);

  uint64_t bitmap = ext4BgBlockBitmap(ext4, group);
  uint8_t *buff = malloc(ext4->blockSize);

  getDiskBytes(buff, EXT4_BLOCK_TO_LBA(ext4, 0, bitmap),
               ext4->blockSize / SECTOR_SIZE);

  uint32_t where = index / 8;
  uint32_t remainder = index % 8;
  buff[where] &= ~(1 << remainder);
  ext4StageMetadata(ext4, EXT4_BLOCK_TO_LBA(ext4, 0, bitmap), buff,
                    ext4->blockSize / SECTOR_SIZE);

  // set the bgdt accordingly
  spinlockAcquire(&ext4->LOCK_BGDT_WRITE);
  if (ext4->bgdt64) {
    uint16_t lo = ext4BgField16(ext4, group, 0x0c);
    *(uint16_t *)(ext4->bgdts + group * ext4->blockGD + 0x0c) = lo + 1;
  } else
    *(uint16_t *)(ext4->bgdts + group * ext4->blockGD + 0x0c) += 1;
  ext4BgdtPushM(ext4);
  spinlockRelease(&ext4->LOCK_BGDT_WRITE);

  // and the superblock
  spinlockAcquire(&ext4->LOCK_SUPERBLOCK_WRITE);
  if (ext4->superblock.feature_incompat & EXT4_F_INCOMPAT_64BIT)
    ext4->superblock.free_blocks_count_hi =
        (ext4SuperFreeBlocks(ext4) - 1) >> 32;
  ext4->superblock.free_blocks_count_lo += 1;
  ext4SuperblockPushM(ext4);
  spinlockRelease(&ext4->LOCK_SUPERBLOCK_WRITE);

  spinlockCntWriteRelease(&ext4->WLOCKS_BLOCK_BITMAP[group]);
  free(buff);
}

force_inline bool isPowerOf(int n, int power) {
  if (n < 1)
    return false;

  while (n % power == 0)
    n /= power;

  return n == 1;
}

// IMPORTANT! Remember to manually set the spinlock **before** calling
void ext4BgdtPushM(Ext4 *ext4) {
  // the one directly below the superblock
  ext4StageMetadata(ext4, ext4->offsetBGDT, (void *)ext4->bgdts,
                    DivRoundUp(ext4->blockGroups * ext4->blockGD, SECTOR_SIZE));

  for (int i = 1; i < ext4->blockGroups; i++) {
    if (!(i == 0 || i == 1 || isPowerOf(i, 3) || isPowerOf(i, 5) ||
          isPowerOf(i, 7)))
      continue;

    // has a backup/copy...
    ext4StageMetadata(
        ext4,
        EXT4_BLOCK_TO_LBA(ext4, 0, i * ext4->superblock.blocks_per_group + 1),
        (void *)ext4->bgdts,
        DivRoundUp(ext4->blockGroups * ext4->blockGD, SECTOR_SIZE));
  }
}

// IMPORTANT! Remember to manually set the spinlock **before** calling
void ext4SuperblockPushM(Ext4 *ext4) {
  ext4StageMetadata(ext4, ext4->offsetSuperblock, (void *)(&ext4->superblock),
                    2);

  for (int i = 1; i < ext4->blockGroups; i++) {
    if (!(i == 0 || i == 1 || isPowerOf(i, 3) || isPowerOf(i, 5) ||
          isPowerOf(i, 7)))
      continue;

    ext4StageMetadata(
        ext4, EXT4_BLOCK_TO_LBA(ext4, 0, i * ext4->superblock.blocks_per_group),
        (void *)(&ext4->superblock), 2);
  }
}