#include <ext4.h>
#include <malloc.h>
#include <string.h>
#include <system.h>
#include <timer.h>
#include <util.h>

static void ext4InitExtentRoot(Ext4Inode *newInode) {
  newInode->flags |= EXT4_EXTENTS_FL;
  Ext4ExtentHeader *hdr = (Ext4ExtentHeader *)newInode->block;
  hdr->magic = EXT4_EXTENT_MAGIC;
  hdr->entries = 0;
  hdr->max = EXT4_INLINE_EXTENTS_MAX;
  hdr->depth = 0;
  hdr->generation = 0;
}

size_t ext4Mkdir(MountPoint *mnt, char *dirname, uint32_t mode,
                 char **symlinkResolve) {
  Ext4 *ext4 = EXT4_PTR(mnt->fsInfo);

  // dirname will be sanitized anyways
  int len = strlength(dirname);
  int lastSlash = 0;
  for (int i = 0; i < len; i++) {
    if (dirname[i] == '/')
      lastSlash = i;
  }

  uint32_t inode = 0;
  if (lastSlash > 0) {
    char *parent = malloc(lastSlash + 1);
    memcpy(parent, dirname, lastSlash);
    parent[lastSlash] = '\0';
    inode = ext4TraversePath(ext4, parent, 0, true, symlinkResolve);
    free(parent);
  } else // if it's trying to open / just set the inode directly
    inode = EXT4_ROOT_INODE;

  if (!inode)
    return ERR(ENOENT);

  // main scope
  size_t ret = 0;

  // various checks
  char *name = &dirname[lastSlash + 1];
  int   nameLen = strlength(name);

  if (!nameLen) // going for /
    return ERR(EEXIST);

  Ext4Inode *inodeContents = ext4InodeFetch(ext4, inode);
  if (!(inodeContents->permission & S_IFDIR)) {
    ret = ERR(ENOTDIR);
    goto cleanup;
  }

  if (ext4Traverse(ext4, inode, name, nameLen)) {
    ret = ERR(EEXIST);
    goto cleanup;
  }

  ext4JournalBegin(ext4);

  size_t time = timerBootUnix + timerTicks / 1000;

  // prepare what we want to write to that inode
  Ext4Inode newInode = {0};
  newInode.permission = S_IFDIR | mode;
  newInode.uid = 0;
  newInode.size = 0; // will get increased by . & .. entries
  newInode.atime = time;
  newInode.ctime = time;
  newInode.mtime = time;
  newInode.dtime = 0; // not yet :p
  newInode.gid = 0;
  newInode.hard_links = 1 + 1; // +1 for the . pointing to itself
  newInode.num_sectors = 0;
  newInode.generation = 0;
  newInode.file_acl = 0;
  newInode.dir_acl = 0;
  newInode.f_block_addr = 0;
  newInode.blocks_high = 0;
  newInode.size_high = 0;
  ext4InitExtentRoot(&newInode);

  // assign an inode
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inode);
  uint32_t newInodeNum = ext4InodeFind(ext4, group);

  // write to it
  ext4InodeModifyM(ext4, newInodeNum, &newInode);

  // create the . and .. entries as children
  ext4DirAllocate(ext4, newInodeNum, &newInode, ".", 1, EXT4_FT_DIR,
                  newInodeNum);
  ext4DirAllocate(ext4, newInodeNum, &newInode, "..", 2, EXT4_FT_DIR, inode);

  // finally, assign it to the parent
  ext4DirAllocate(ext4, inode, inodeContents, name, nameLen, EXT4_FT_DIR,
                  newInodeNum);
  inodeContents->hard_links++;
  ext4InodeModifyM(ext4, inode, inodeContents);

  ext4JournalCommit(ext4);

cleanup:
  free(inodeContents);
  return ret;
}

size_t ext4Touch(MountPoint *mnt, char *filename, uint32_t mode,
                 char **symlinkResolve) {
  Ext4 *ext4 = EXT4_PTR(mnt->fsInfo);

  // dirname will be sanitized anyways
  int len = strlength(filename);
  int lastSlash = 0;
  for (int i = 0; i < len; i++) {
    if (filename[i] == '/')
      lastSlash = i;
  }

  uint32_t inode = 0;
  if (lastSlash > 0) {
    char *parent = malloc(lastSlash + 1);
    memcpy(parent, filename, lastSlash);
    parent[lastSlash] = '\0';
    inode = ext4TraversePath(ext4, parent, 0, true, symlinkResolve);
    free(parent);
  } else // if it's trying to open / just set the inode directly
    inode = EXT4_ROOT_INODE;

  if (!inode)
    return ERR(ENOENT);

  // main scope
  size_t ret = 0;

  // various checks
  char *name = &filename[lastSlash + 1];
  int   nameLen = strlength(name);

  if (!nameLen) // going for /
    return ERR(EISDIR);

  Ext4Inode *inodeContents = ext4InodeFetch(ext4, inode);
  if (!(inodeContents->permission & S_IFDIR)) {
    ret = ERR(ENOTDIR);
    goto cleanup;
  }

  if (ext4Traverse(ext4, inode, name, nameLen)) {
    ret = ERR(EEXIST);
    goto cleanup;
  }

  ext4JournalBegin(ext4);

  size_t time = timerBootUnix + timerTicks / 1000;

  // prepare what we want to write to that inode
  Ext4Inode newInode = {0};
  newInode.permission = S_IFREG | mode;
  newInode.uid = 0;
  newInode.size = 0;
  newInode.atime = time;
  newInode.ctime = time;
  newInode.mtime = time;
  newInode.dtime = 0; // not yet :p
  newInode.gid = 0;
  newInode.hard_links = 1;
  newInode.num_sectors = 0;
  newInode.generation = 0;
  newInode.file_acl = 0;
  newInode.dir_acl = 0;
  newInode.f_block_addr = 0;
  newInode.blocks_high = 0;
  newInode.size_high = 0;
  ext4InitExtentRoot(&newInode);

  // assign an inode
  uint32_t group = EXT4_INODE_TO_BLOCK_GROUP(ext4, inode);
  uint32_t newInodeNum = ext4InodeFind(ext4, group);

  // write to it
  ext4InodeModifyM(ext4, newInodeNum, &newInode);

  // finally, assign it to the parent
  ext4DirAllocate(ext4, inode, inodeContents, name, nameLen, EXT4_FT_REG_FILE,
                  newInodeNum);

  ext4JournalCommit(ext4);

cleanup:
  free(inodeContents);
  return ret;
}