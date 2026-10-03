#include <dev.h>
#include <disk.h>
#include <ext2.h>
#include <ext4.h>
#include <fat32.h>
#include <linked_list.h>
#include <malloc.h>
#include <proc.h>
#include <string.h>
#include <sys.h>
#include <system.h>
#include <task.h>
#include <util.h>
#include <vfs.h>

// Different mount point separation
// Copyright (C) 2024 Panagiotis

// todo: add LL lock to mountPoints

// Detaches a mount point and frees what it owns.
//
// This deliberately does not panic, and it is deliberately not reachable from
// umount(2) yet. fsMount calls it on every failure path, so once mount(2) hands
// user-controlled input to fsMount, a panic here would turn every failed mount
// attempt - a typo, an unknown fstype, a full disk - into a kernel panic. It
// only unregisters the mount point and releases memory; there is still no
// per-filesystem unmount hook and no writeback barrier, so tearing down a
// filesystem that other tasks still hold open would hand them dangling
// pointers. Until both of those exist the only callers are fsMount's own
// failure paths, where the mount point was never visible to anyone.
bool fsUnmount(MountPoint *mnt) {
  LinkedListUnregister(&dsMountPoint, sizeof(MountPoint), mnt);

  // fsMount zeroes every MountPoint it allocates, so one that bailed out
  // partway through still has a valid (or NULL) fsInfo here.
  if (mnt->fsInfo)
    free(mnt->fsInfo);

  free(mnt->prefix);
  free(mnt);

  return true;
}

bool isFat(mbr_partition *mbr) {
  uint8_t *rawArr = (uint8_t *)malloc(SECTOR_SIZE);
  getDiskBytes(rawArr, mbr->lba_first_sector, 1);

  bool ret = (rawArr[66] == 0x28 || rawArr[66] == 0x29);

  free(rawArr);

  return ret;
}

bool isExt2(mbr_partition *mbr) { return mbr->type == 0x83; }

// Reports which filesystem a partition actually holds, without mounting it.
//
// fsMount works this out for itself from the partition's boot sector, but only
// after it has committed to the mount. mount(2) needs the answer up front so it
// can tell the caller that asking for ext4 on an ext2 partition failed, rather
// than mounting ext2 and reporting success.
bool fsDetectFilesystem(uint32_t disk, uint8_t partition, FS *out) {
  mbr_partition mbr;
  if (!openDisk(disk, partition, &mbr))
    return false;

  if (isFat(&mbr)) {
    *out = FS_FATFS;
    return true;
  }
  if (isExt2(&mbr)) {
    *out = probeExt4(&mbr) ? FS_EXT4 : FS_EXT2;
    return true;
  }

  return false;
}

// prefix MUST end with '/': /mnt/handle/
MountPoint *fsMount(char *prefix, CONNECTOR connector, uint32_t disk,
                    uint8_t partition) {
  MountPoint *mount =
      (MountPoint *)LinkedListAllocate(&dsMountPoint, sizeof(MountPoint));

  uint32_t strlen = strlength(prefix);
  mount->prefix = (char *)(malloc(strlen + 1));
  memcpy(mount->prefix, prefix, strlen);
  mount->prefix[strlen] = '\0'; // null terminate

  mount->disk = disk;
  mount->partition = partition;
  mount->connector = connector;

  bool ret = false;
  switch (connector) {
  case CONNECTOR_AHCI:
    if (!openDisk(disk, partition, &mount->mbr)) {
      fsUnmount(mount);
      return 0;
    }

    if (isFat(&mount->mbr)) {
      mount->filesystem = FS_FATFS;
      ret = fat32Mount(mount);
    } else if (isExt2(&mount->mbr)) {
      if (probeExt4(&mount->mbr)) {
        mount->filesystem = FS_EXT4;
        ret = ext4Mount(mount);
      } else {
        // fall through to ext2, which validates its own superblock
        mount->filesystem = FS_EXT2;
        ret = ext2Mount(mount);
      }
    }
    break;
  case CONNECTOR_DEV:
    mount->filesystem = FS_DEV;
    ret = devMount(mount);
    break;
  case CONNECTOR_SYS:
    mount->filesystem = FS_SYS;
    ret = sysMount(mount);
    break;
  case CONNECTOR_PROC:
    mount->filesystem = FS_PROC;
    ret = procMount(mount);
    break;
  default:
    debugf("[vfs] Tried to mount with bad connector! id{%d}\n", connector);
    ret = 0;
    break;
  }

  if (!ret) {
    fsUnmount(mount);
    return 0;
  }

  if (!systemDiskInit && strlength(prefix) == 1 && prefix[0] == '/')
    systemDiskInit = true;
  return mount;
}

typedef struct {
  char       *filename;
  MountPoint *largestAddr;
  uint32_t    largestLen;
} MountPointCbArgs;
void fsDetermineMountPointCb(void *data, void *ctx) {
  MountPoint       *browse = data;
  MountPointCbArgs *p = ctx;

  size_t len = strlength(browse->prefix) - 1; // without trailing /
  if (len >= p->largestLen && memcmp(p->filename, browse->prefix, len) == 0 &&
      (p->filename[len] == '/' || p->filename[len] == '\0')) {
    p->largestAddr = browse;
    p->largestLen = len;
  }
}

MountPoint *fsDetermineMountPoint(char *filename) {
  MountPointCbArgs args = {.filename = filename};
  LinkedListTraverse(&dsMountPoint, fsDetermineMountPointCb, &args);
  return args.largestAddr;
}

// make SURE to free both! also returns non-safe filename, obviously
char *fsResolveSymlink(MountPoint *mnt, char *symlink) {
  int symlinkLength = strlength(symlink);
  int prefixLength = strlength(mnt->prefix) - 1; // remove slash

  char *ret = 0;
  if (symlink[0] == '/') {
    // needs prefixing
    ret = malloc(prefixLength + symlinkLength + 1);
    memcpy(ret, mnt->prefix, prefixLength);
    memcpy(&ret[prefixLength], symlink, symlinkLength);
    ret[prefixLength + symlinkLength] = '\0';
  } else if (symlink[0] == '!') {
    // doesn't need prefixing
    ret = malloc(symlinkLength + 1);
    memcpy(ret, &symlink[1], symlinkLength);
    ret[symlinkLength] = '\0';
  }

  return ret;
}
