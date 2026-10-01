#include <ext4.h>
#include <malloc.h>
#include <string.h>
#include <system.h>
#include <timer.h>
#include <util.h>

#include <linked_list.h>
#include <murmur_hash.h>

uint32_t ext4Traverse(Ext4 *ext4, size_t initInode, char *search,
                      size_t searchLength) {
  uint32_t   ret = 0;
  Ext4Inode *ino = ext4InodeFetch(ext4, initInode);
  uint8_t   *names = (uint8_t *)malloc(ext4->blockSize);

  Ext4LookupControl control = {0};
  size_t            blockNum = 0;

  ext4BlockFetchInit(ext4, &control);

  int blocksContained =
      DivRoundUp(COMBINE_64(ino->size_high, ino->size), ext4->blockSize);
  for (int i = 0; i < blocksContained; i++) {
    uint64_t block = ext4BlockFetch(ext4, ino, initInode, &control, blockNum);
    blockNum++;
    if (!block)
      break;
    Ext4Directory *dir = (Ext4Directory *)names;

    getDiskBytes(names, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                 ext4->blockSize / SECTOR_SIZE);

    while (((size_t)dir - (size_t)names) < ext4->blockSize) {
      if (!dir->inode) {
        goto traverse;
        continue;
      }
      if (dir->filenameLength == searchLength &&
          memcmp(dir->filename, search, searchLength) == 0) {
        ret = dir->inode;
        goto cleanup;
      }
    traverse:
      dir = (void *)((size_t)dir + dir->size);
    }
  }

cleanup:
  ext4BlockFetchCleanup(&control);
  free(ino);
  free(names);
  return ret;
}

typedef struct Ext4TrCbStr {
  char  *filename;
  size_t len;

  uint64_t hash;
} Ext4TrCbStr;
bool ext4TraverseCbStr(void *data, void *ctx) {
  Ext4TrCbStr     *target = ctx;
  Ext4FoundObject *obj = data;
  if (target->len != obj->filenameLen)
    return false;
  // use hashing only for rejecting, dont depend on it
  if (target->hash != obj->hash)
    return false;
  return memcmp(obj->filename, target->filename, target->len) == 0;
}

uint32_t ext4TraversePath(Ext4 *ext4, char *path, Ext4FoundObject **retObj,
                          bool follow, char **symlinkResolve) {
  uint32_t curr = EXT4_ROOT_INODE;
  size_t   len = strlength(path);

  if (len == 1) { // meaning it's trying to open /
    if (retObj)
      *retObj = (Ext4FoundObject *)ext4->rootObject.firstObject;
    return 2;
  }

  uint32_t ret = 0;

  Ext4LookupControl control = {0};
  ext4BlockFetchInit(ext4, &control);

  spinlockAcquire(&ext4->LOCK_OBJECT);
  Ext4FoundObject *obj = (Ext4FoundObject *)ext4->rootObject.firstObject;

  Ext4TrCbStr cb = {0};

  int lastslash = 0;
  for (int i = 1; i < len; i++) { // 1 to skip /[...]
    bool last = i == (len - 1);

    if (path[i] == '/' || last) {
      size_t length = i - lastslash - 1;
      if (last) // no need to remove trailing /
        length += 1;

      cb.filename = path + lastslash + 1;
      cb.len = length;
      cb.hash = murmur_hash(cb.filename, cb.len, 67);
      Ext4FoundObject *objNext =
          LinkedListSearch(&obj->inner, ext4TraverseCbStr, &cb);

      if (objNext) {
        obj = objNext;
        curr = objNext->inode;
      } else
        curr = ext4Traverse(ext4, curr, path + lastslash + 1, length);

      if (!curr) {
        ret = curr;
        goto cleanup;
      }

      if (!objNext) {
        // create it for later traverses
        Ext4FoundObject *new =
            LinkedListAllocate(&obj->inner, sizeof(Ext4FoundObject));
        LinkedListInit(&new->inner, sizeof(Ext4FoundObject));
        new->inode = curr;
        new->filename = malloc(cb.len + 1);
        memcpy(new->filename, cb.filename, cb.len);
        new->filename[cb.len] = '\0';
        new->filenameLen = cb.len;
        new->hash = cb.hash;
        objNext = new;
        obj = objNext;
      }

      Ext4Inode *inode = ext4InodeFetch(ext4, curr);
      if ((inode->permission & 0xF000) == EXT4_S_IFLNK && (!last || follow)) {
        char *start = 0;
        char *symlinkTarget = 0;
        if (COMBINE_64(inode->size_high, inode->size) > 60) {
          assert(COMBINE_64(inode->size_high, inode->size) < ext4->blockSize);

          start = (char *)calloc(ext4->blockSize + 1, 1);
          symlinkTarget = (char *)calloc(len + inode->size + 2,
                                         1); // extra just in case
          uint64_t block = ext4BlockFetch(ext4, inode, curr, &control, 0);
          getDiskBytes((uint8_t *)start, EXT4_BLOCK_TO_LBA(ext4, 0, block),
                       ext4->blockSize / SECTOR_SIZE);
        } else {
          start = (char *)inode->block;
          symlinkTarget = (char *)calloc(len + 60 + 2, 1); // extra just in case
        }
        *symlinkResolve = symlinkTarget;

        if (start[0] != '/') {
          memcpy(symlinkTarget, path, lastslash + 1);
          memcpy(&symlinkTarget[lastslash + 1], start, inode->size);
          memcpy(&symlinkTarget[lastslash + 1 + inode->size],
                 &path[lastslash + 1 + length], len - (lastslash + 1 + length));
        } else {
          symlinkTarget[0] = '!';
          memcpy(&symlinkTarget[1], start, inode->size);
        }
        if (COMBINE_64(inode->size_high, inode->size) > 60)
          free(start);
        free(inode);

        ret = 0;
        goto cleanup;
      }
      bool notdir = !(inode->permission & S_IFDIR);
      free(inode);

      // return fail or last's success
      if (!curr || i == (len - 1)) {
        ret = curr;
        goto cleanup;
      }

      if (notdir) {
        // if by this point it's not a directory, we're screwed
        ret = 0;
        goto cleanup;
      }

      lastslash = i;
    }
  }

cleanup:
  ext4BlockFetchCleanup(&control);
  if (ret >= 2 && retObj) {
    atomic_fetch_add(&obj->openFds, 1);
    *retObj = obj;
  }
  spinlockRelease(&ext4->LOCK_OBJECT);
  return ret;
}