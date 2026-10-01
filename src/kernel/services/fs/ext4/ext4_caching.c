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

void ext4CacheAddSecurely(MountPoint *mnt, Ext4FoundObject *global,
                          uint8_t *buff, size_t blockIndex, size_t blocks) {
  Ext4 *ext4 = EXT4_PTR(mnt->fsInfo);

  spinlockCntWriteAcquire(&global->WLOCK_CACHE);
  // find anything as a start that is close
  Ext4CacheObject *cacheObj = global->firstCacheObj;
  Ext4CacheObject *lastCaught = global->firstCacheObj;
  while (cacheObj) {
    if (cacheObj->blockIndex >= blockIndex &&
        cacheObj->blockIndex < (blockIndex + blocks))
      break;
    lastCaught = cacheObj;
    cacheObj = cacheObj->next;
  }
  if (!cacheObj) {
    // well, we got no problems
    Ext4CacheObject *browse = global->firstCacheObj;
    bool             allAreSmaller = false;
    while (browse) {
      if (browse->blockIndex < blockIndex) {
        allAreSmaller = true;
        browse = browse->next;
      } else if (browse->blockIndex == blockIndex)
        assert(false); // just checked above
      else { // it's really the first that is bigger (comes afterwards)
        browse = browse->prev;
        break;
      }
    }
    Ext4CacheObject *target = calloc(sizeof(Ext4CacheObject), 1);
    mnt->blocksCached += blocks;
    target->blockIndex = blockIndex;
    target->blocks = blocks;
    target->buff = buff;
    if (!global->firstCacheObj) { // simple, no root
      global->firstCacheObj = target;
    } else if (browse) { // simple, all are smaller up to one which we use
      Ext4CacheObject *next = browse->next;
      browse->next = target;
      target->prev = browse;

      target->next = next;
      if (next)
        next->prev = target;
    } else if (allAreSmaller && lastCaught) {
      // all leading to the end are smaller, place it on the end
      lastCaught->next = target;
      target->prev = lastCaught;
    } else if (!allAreSmaller) {
      // all leading to the end are bigger (browse is ->prev of first, hence
      // zero)
      target->next = global->firstCacheObj;
      global->firstCacheObj = target;
      if (target->next)
        target->next->prev = target;
    } else
      assert(false);
  } else {
    // we got a caching layer in that region already
    Ext4CacheObject *browse = cacheObj;
    while (browse) {
      if (!(browse->blockIndex >= blockIndex &&
            browse->blockIndex < (blockIndex + blocks)))
        break;
      // solve the problem:
      // for now the quickest method is to try and remove it.
      VirtualFree(
          browse->buff,
          DivRoundUp((browse->blocks + 1) * ext4->blockSize, BLOCK_SIZE));
      mnt->blocksCached -= browse->blocks;

      Ext4CacheObject *next = browse->next;
      if (!browse->prev) {
        // first object
        global->firstCacheObj = browse->next;
        if (global->firstCacheObj)
          global->firstCacheObj->prev = 0;
      } else {
        Ext4CacheObject *prev = browse->prev;
        prev->next = browse->next;
        if (browse->next)
          browse->next->prev = prev;
      }
      free(browse);
      browse = next;
    }

    // continue normally now
    spinlockCntWriteRelease(&global->WLOCK_CACHE);
    ext4CacheAddSecurely(mnt, global, buff, blockIndex, blocks);
    return;
  }
  spinlockCntWriteRelease(&global->WLOCK_CACHE);
}

void ext4CachePush(Ext4 *ext4, Ext4OpenFd *fd) {
  // todo: base the LRU off unix timestamps and some sort of daemon
  // that is instead of the typical push-to-front ordering...
}