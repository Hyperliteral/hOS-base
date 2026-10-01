#include <disk.h>
#include <ext4.h>
#include <malloc.h>
#include <string.h>
#include <system.h>
#include <util.h>

// ---- little helpers, jbd2 is big-endian ----

static inline uint32_t ext4JBe32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static inline void ext4JBe32Put(uint8_t *p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static inline uint32_t ext4JAbs(Ext4 *ext4, uint32_t pos, uint32_t delta) {
  uint32_t areaLen = ext4->journal.maxlen - ext4->journal.first;
  pos -= ext4->journal.first;
  pos = (pos + delta) % areaLen;
  return ext4->journal.first + pos;
}

static size_t ext4JournalBlockLba(Ext4 *ext4, uint32_t jblock) {
  return EXT4_BLOCK_TO_LBA(
      ext4, 0, ext4->journal.blocks[jblock % ext4->journal.blockCount]);
}

// the superblock lives in journal block 0, flush the state we track
static void ext4JournalSbWrite(Ext4 *ext4) {
  uint8_t *sb = (uint8_t *)malloc(ext4->blockSize);
  memset(sb, 0, ext4->blockSize);

  ext4JBe32Put(sb + 0x00, EXT4_JBD2_MAGIC);
  ext4JBe32Put(sb + 0x04, EXT4_JBD2_SUPER_V2);
  ext4JBe32Put(sb + 0x08, ext4->journal.sequence);
  ext4JBe32Put(sb + 0x0c, ext4->blockSize);
  ext4JBe32Put(sb + 0x10, ext4->journal.maxlen);
  ext4JBe32Put(sb + 0x14, ext4->journal.first);
  ext4JBe32Put(sb + 0x18, ext4->journal.sequence);
  ext4JBe32Put(sb + 0x1c, ext4->journal.start);
  ext4JBe32Put(sb + 0x20, 0); // errno
  ext4JBe32Put(sb + 0x24, 0); // compat features
  ext4JBe32Put(sb + 0x28, EXT4_JBD2_FEATURE_INCOMPAT_REVOKE);
  ext4JBe32Put(sb + 0x2c, 0); // ro-compat features
  ext4JBe32Put(sb + 0x40, 1); // nr_users (we never share)

  setDiskBytes(sb, ext4JournalBlockLba(ext4, 0), ext4->blockSize / SECTOR_SIZE);
  free(sb);
}

typedef struct Ext4PendingBlock {
  uint32_t target; // FS block number that the data must be written to
  uint32_t data;   // journal block that holds the data
} Ext4PendingBlock;

// replays the single un-applied transaction sitting at journal->start, if any
static void ext4JournalRecover(Ext4 *ext4) {
  Ext4Journal *j = &ext4->journal;
  uint32_t     areaLen = j->maxlen - j->first;
  if (!areaLen)
    return;

  uint32_t pos = j->start;
  if (pos < j->first || pos >= j->maxlen)
    pos = j->first;

  uint8_t *bh = (uint8_t *)malloc(ext4->blockSize);

  // revoked blocks (foreign journals), honoured so replay doesn't resurrect
  // a block that has since been freed & reused
  enum { MAX_REVOKE = 256 };
  uint32_t revoke[MAX_REVOKE];
  int      nRevoke = 0;

  Ext4PendingBlock pending[256];
  int              pendingCount = 0;

  uint32_t scanned = 0;
  bool     haveCommit = false;

  while (pendingCount < 256 && scanned < areaLen) {
    getDiskBytes(bh, ext4JournalBlockLba(ext4, pos),
                 ext4->blockSize / SECTOR_SIZE);
    if (ext4JBe32(bh) != EXT4_JBD2_MAGIC)
      break; // we reached unused journal space, nothing here
    uint32_t type = ext4JBe32(bh + 4);
    uint32_t seq = ext4JBe32(bh + 8);

    if (type != EXT4_JBD2_REVOKE && seq != j->sequence)
      break; // not our transaction, stop before applying anything foreign

    if (type == EXT4_JBD2_DESCRIPTOR) {
      int ntag = 0;
      for (int off = 12; off + 8 <= ext4->blockSize; off += 8) {
        uint16_t flags = (uint16_t)((bh[off + 6] << 8) | bh[off + 7]);
        if (pendingCount < 256) {
          pending[pendingCount].target = ext4JBe32(bh + off);
          pending[pendingCount].data = ext4JAbs(ext4, pos, 1 + ntag);
          pendingCount++;
        }
        ntag++;
        if (flags & EXT4_JBD2_FLAG_LAST_TAG)
          break;
      }
      pos = ext4JAbs(ext4, pos, 1 + ntag); // skip descriptor + its data
      scanned += 1 + ntag;
      continue;
    }

    if (type == EXT4_JBD2_COMMIT) {
      haveCommit = true;
      pos = ext4JAbs(ext4, pos, 1);
      break;
    }

    if (type == EXT4_JBD2_REVOKE) {
      uint32_t used = ext4JBe32(bh + 12); // r_count, bytes used up to
      for (int off = 16; off + 4 <= (int)used && nRevoke < MAX_REVOKE; off += 4)
        revoke[nRevoke++] = ext4JBe32(bh + off);
      pos = ext4JAbs(ext4, pos, 1);
      scanned++;
      continue;
    }

    break; // leftover garbage
  }

  if (haveCommit) {
    // apply the transaction's blocks to their final locations
    for (int i = 0; i < pendingCount; i++) {
      bool revoked = false;
      for (int k = 0; k < nRevoke; k++) {
        if (revoke[k] == pending[i].target) {
          revoked = true;
          break;
        }
      }
      if (revoked)
        continue;
      getDiskBytes(bh, ext4JournalBlockLba(ext4, pending[i].data),
                   ext4->blockSize / SECTOR_SIZE);
      setDiskBytes(bh, EXT4_BLOCK_TO_LBA(ext4, 0, pending[i].target),
                   ext4->blockSize / SECTOR_SIZE);
    }

    debugf("[ext4] Journal replay: committed %d blocks, sequence %d\n",
           pendingCount, j->sequence);
  } else if (scanned) {
    debugf(
        "[ext4] Journal: no commit found, discarding incomplete transaction\n");
  } else {
    debugf("[ext4] Journal: clean\n");
  }

  // whoever wrote the transaction is done with it once we've (re)applied it
  if (haveCommit) {
    j->start = pos;
    j->sequence = j->sequence + 1;
  }
  ext4JournalSbWrite(ext4);

  free(bh);
}

void ext4JournalInit(Ext4 *ext4) {
  Ext4Journal *j = &ext4->journal;
  j->active = false;
  j->enabled = false;
  j->depth = 0;
  j->staged = 0;

  if (!(ext4->superblock.feature_compat & EXT4_F_COMPAT_HAS_JOURNAL))
    return; // no journal on this volume, keep writing metadata directly

  uint32_t jnum = ext4->superblock.journal_inode;
  if (!jnum)
    return; // external journal device, unsupported

  Ext4Inode *jino = ext4InodeFetch(ext4, jnum);
  if (!jino || !(jino->flags & EXT4_EXTENTS_FL)) {
    if (jino)
      free(jino);
    return;
  }

  // flatten the journal inode's extent tree into a physical block list
  Ext4LookupControl ctrl = {0};
  ext4BlockFetchInit(ext4, &ctrl);
  int       cap = 16;
  int       cnt = 0;
  uint32_t *list = (uint32_t *)malloc(cap * sizeof(uint32_t));
  for (size_t i = 0; i < (ext4->superblock.blocks_per_group * 8); i++) {
    uint64_t b = ext4BlockFetch(ext4, jino, jnum, &ctrl, i);
    if (!b)
      break;
    if (cnt >= cap) {
      cap *= 2;
      uint32_t *nl = (uint32_t *)malloc(cap * sizeof(uint32_t));
      memcpy(nl, list, cnt * sizeof(uint32_t));
      free(list);
      list = nl;
    }
    list[cnt++] = b;
  }
  ext4BlockFetchCleanup(&ctrl);
  free(jino);
  if (!cnt) {
    free(list);
    return;
  }

  j->blocks = list;
  j->blockCount = cnt;

  uint8_t *tmp = (uint8_t *)malloc(ext4->blockSize);
  getDiskBytes(tmp, ext4JournalBlockLba(ext4, 0),
               ext4->blockSize / SECTOR_SIZE);
  if (ext4JBe32(tmp) != EXT4_JBD2_MAGIC) {
    free(tmp);
    free(list);
    debugf("[ext4] Journal inode present but no valid journal superblock!\n");
    return;
  }

  uint32_t jbs = ext4JBe32(tmp + 0x0c);
  uint32_t jmaxlen = ext4JBe32(tmp + 0x10);
  uint32_t jfirst = ext4JBe32(tmp + 0x14);
  j->sequence = ext4JBe32(tmp + 0x18);
  j->start = ext4JBe32(tmp + 0x1c);
  uint32_t jIncompat = ext4JBe32(tmp + 0x28);
  free(tmp);

  if (jbs != ext4->blockSize) {
    debugf("[ext4] Journal block size mismatch! %d != %d\n", jbs,
           ext4->blockSize);
    free(list);
    return;
  }
  if (!jmaxlen || jmaxlen > j->blockCount || !jfirst || jfirst >= jmaxlen) {
    debugf("[ext4] Journal geometry is broken! maxlen{%d} first{%d}\n", jmaxlen,
           jfirst);
    free(list);
    return;
  }
  if (jIncompat & ~EXT4_JBD2_FEATURE_INCOMPAT_REVOKE) {
    debugf("[ext4] Journal uses unsupported features {incompat:%x}, "
           "journaling disabled!\n",
           jIncompat);
    free(list);
    return;
  }

  j->maxlen = jmaxlen;
  j->first = jfirst;
  if (!j->sequence)
    j->sequence = 1;
  if (!j->start || j->start < j->first || j->start >= j->maxlen)
    j->start = j->first;

  j->enabled = true;

  ext4JournalRecover(ext4);

  j->active = true;
}

void ext4JournalBegin(Ext4 *ext4) {
  if (!ext4->journal.active)
    return;
  spinlockAcquire(&ext4->LOCK_JOURNAL);
  ext4->journal.depth++;
  spinlockRelease(&ext4->LOCK_JOURNAL);
}

void ext4JournalAbort(Ext4 *ext4) {
  Ext4Journal *j = &ext4->journal;
  if (!j->active)
    return;

  spinlockAcquire(&ext4->LOCK_JOURNAL);
  j->depth = 0;
  Ext4StagedBlock *b = j->staged;
  while (b) {
    Ext4StagedBlock *next = b->next;
    free(b->data);
    free(b);
    b = next;
  }
  j->staged = 0;
  spinlockRelease(&ext4->LOCK_JOURNAL);
}

// journal a metadata block write, or perform it directly when there is no
// open transaction (or the journal is not in use)
void ext4StageMetadata(Ext4 *ext4, size_t lba, uint8_t *data, size_t sectors) {
  Ext4Journal *j = &ext4->journal;
  if (!j->active || j->depth <= 0) {
    setDiskBytes(data, lba, sectors);
    return;
  }

  spinlockAcquire(&ext4->LOCK_JOURNAL);

  size_t left = sectors * SECTOR_SIZE;
  size_t p = lba;
  while (left > 0) {
    uint32_t blkNum =
        (uint32_t)((p - ext4->offsetBase) * SECTOR_SIZE / ext4->blockSize);
    size_t blkLba = EXT4_BLOCK_TO_LBA(ext4, 0, blkNum);
    size_t off = (p - blkLba) * SECTOR_SIZE;
    size_t chunk = MIN(ext4->blockSize - off, left);

    Ext4StagedBlock *s = j->staged;
    while (s && s->blockNum != blkNum)
      s = s->next;
    if (!s) {
      s = (Ext4StagedBlock *)malloc(sizeof(Ext4StagedBlock));
      s->blockNum = blkNum;
      s->data = (uint8_t *)malloc(ext4->blockSize);
      getDiskBytes(s->data, blkLba, ext4->blockSize / SECTOR_SIZE);
      s->next = j->staged;
      j->staged = s;
    }

    memcpy(s->data + off, data + (left - chunk), chunk);
    p += chunk / SECTOR_SIZE;
    left -= chunk;
  }

  spinlockRelease(&ext4->LOCK_JOURNAL);
}

void ext4JournalCommit(Ext4 *ext4) {
  Ext4Journal *j = &ext4->journal;
  if (!j->active)
    return;

  spinlockAcquire(&ext4->LOCK_JOURNAL);
  if (j->depth > 0)
    j->depth--;
  if (j->depth > 0 || !j->staged) {
    spinlockRelease(&ext4->LOCK_JOURNAL);
    return;
  }

  int              tagsPerDesc = (ext4->blockSize - 12) / 8;
  uint8_t         *buf = (uint8_t *)malloc(ext4->blockSize);
  uint32_t         seq = j->sequence;
  uint32_t         pos = j->start;
  uint8_t         *work = (uint8_t *)malloc(ext4->blockSize);
  Ext4StagedBlock *b = j->staged;

  // descriptor + data runs + single commit block
  while (b) {
    // snapshot this descriptor's segment
    Ext4StagedBlock *seg[256];
    int              segN = 0;
    while (b && segN < tagsPerDesc && segN < 256) {
      seg[segN++] = b;
      b = b->next;
    }

    // build the descriptor block
    memset(buf, 0, ext4->blockSize);
    ext4JBe32Put(buf, EXT4_JBD2_MAGIC);
    ext4JBe32Put(buf + 4, EXT4_JBD2_DESCRIPTOR);
    ext4JBe32Put(buf + 8, seq);
    for (int k = 0; k < segN; k++) {
      int  off = 12 + k * 8;
      bool escape = ext4JBe32(seg[k]->data) == EXT4_JBD2_MAGIC;
      ext4JBe32Put(buf + off, seg[k]->blockNum);
      buf[off + 6] = 0;
      buf[off + 7] = EXT4_JBD2_FLAG_SAME_UUID |
                     (k == segN - 1 ? EXT4_JBD2_FLAG_LAST_TAG : 0) |
                     (escape ? EXT4_JBD2_FLAG_ESCAPE : 0);
    }
    setDiskBytes(buf, ext4JournalBlockLba(ext4, pos),
                 ext4->blockSize / SECTOR_SIZE);

    // write the data blocks themselves
    for (int k = 0; k < segN; k++) {
      pos = ext4JAbs(ext4, pos, 1);
      bool escape = ext4JBe32(seg[k]->data) == EXT4_JBD2_MAGIC;
      if (escape) {
        memcpy(work, seg[k]->data, ext4->blockSize);
        memset(work, 0, 4); // escaped per the spec, first word zeroed
        setDiskBytes(work, ext4JournalBlockLba(ext4, pos),
                     ext4->blockSize / SECTOR_SIZE);
      } else {
        setDiskBytes(seg[k]->data, ext4JournalBlockLba(ext4, pos),
                     ext4->blockSize / SECTOR_SIZE);
      }
    }
  }

  // commit sentry
  pos = ext4JAbs(ext4, pos, 1);
  memset(buf, 0, ext4->blockSize);
  ext4JBe32Put(buf, EXT4_JBD2_MAGIC);
  ext4JBe32Put(buf + 4, EXT4_JBD2_COMMIT);
  ext4JBe32Put(buf + 8, seq);
  setDiskBytes(buf, ext4JournalBlockLba(ext4, pos),
               ext4->blockSize / SECTOR_SIZE);

  // checkpoint: now that the transaction is durable, apply it to the fs
  for (b = j->staged; b; b = b->next)
    setDiskBytes(b->data, EXT4_BLOCK_TO_LBA(ext4, 0, b->blockNum),
                 ext4->blockSize / SECTOR_SIZE);

  // and finally roll the start/sequence past the transaction
  pos = ext4JAbs(ext4, pos, 1);
  j->start = pos;
  j->sequence = seq + 1;
  ext4JournalSbWrite(ext4);

  free(work);
  free(buf);

  // drop the staging
  b = j->staged;
  while (b) {
    Ext4StagedBlock *next = b->next;
    free(b->data);
    free(b);
    b = next;
  }
  j->staged = 0;

  spinlockRelease(&ext4->LOCK_JOURNAL);
}