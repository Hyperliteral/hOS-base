#include <dev.h>
#include <malloc.h>
#include <task.h>
#include <util.h>
#include <string.h>

#include <fb.h>
#include <syscalls.h>

// todo: extremely primitive and bad
size_t randomRead(OpenFile *fd, uint8_t *out, size_t limit) {
  size_t i = 0;
  
  // Fill in 4-byte chunks safely using memcpy to avoid unaligned access
  while (i + sizeof(uint32_t) <= limit) {
    uint32_t r = rand();
    memcpy(out + i, &r, sizeof(uint32_t));
    i += sizeof(uint32_t);
  }
  
  // Fill any remaining bytes forward
  while (i < limit) {
    out[i++] = (uint8_t)(rand() & 0xFF);
  }
  
  return limit;
}
VfsHandlers handleRandom = {.read = randomRead, .stat = fakefsFstat};

size_t zeroRead(OpenFile *fd, uint8_t *out, size_t limit) {
  memset(out, 0, limit);
  return limit;
}

size_t zeroWrite(OpenFile *fd, uint8_t *in, size_t limit) {
  // Discard writes and report success for all bytes written
  return limit;
}
VfsHandlers handleZero = {
  .read = zeroRead,
  .write = zeroWrite,
  .stat = fakefsFstat
};

size_t devTtyOpen(char *filename, int flags, int mode, OpenFile *fd,
                  char **symlinkResolve) {
  if (currentTask->ctrlPty == -1)
    return ERR(ENXIO);
  char buff[128] = {0};
  snprintf(buff, 128, "/pts/%d", currentTask->ctrlPty);
  fd->handlers = &handlePts;
  size_t ret = ptsOpen(buff, flags, mode, fd, symlinkResolve);
  return ret;
}
VfsHandlers handleTty = {.open = devTtyOpen};

Fakefs rootDev = {0};

void devSetup() {
  FakefsFile *rootFile = (FakefsFile *)rootDev.rootFile.firstObject;
  fakefsAddFile(&rootDev, rootFile, "stdin", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &stdio);
  fakefsAddFile(&rootDev, rootFile, "stdout", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &stdio);
  fakefsAddFile(&rootDev, rootFile, "stderr", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &stdio);
  fakefsAddFile(&rootDev, rootFile, "tty", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &handleTty);
  fakefsAddFile(&rootDev, rootFile, "fb0", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &fb0);
  fakefsAddFile(&rootDev, rootFile, "null", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &handleNull);
  fakefsAddFile(&rootDev, rootFile, "zero", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &handleZero);
  fakefsAddFile(&rootDev, rootFile, "random", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &handleRandom);
  fakefsAddFile(&rootDev, rootFile, "urandom", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &handleRandom);

  initiatePtyInterface();
  fakefsAddFile(&rootDev, rootFile, "ptmx", 0, S_IFCHR | S_IRUSR | S_IWUSR,
                &handlePtmx);
  FakefsFile *pts =
      fakefsAddFile(&rootDev, rootFile, "pts", 0, S_IFDIR | S_IRUSR | S_IWUSR,
                    &fakefsRootHandlers);
  fakefsAddFile(&rootDev, pts, "*", 0, S_IFCHR | S_IRUSR | S_IWUSR, &handlePts);

  inputFakedir =
      fakefsAddFile(&rootDev, rootFile, "input", 0, S_IFDIR | S_IRUSR | S_IWUSR,
                    &fakefsRootHandlers);
}

// Allows storage drivers (like AHCI) to dynamically register disks into /dev
void devRegisterBlockDevice(const char *name, VfsHandlers *handlers) {
  if (!rootDev.rootFile.firstObject) {
    fakefsSetupRoot(&rootDev.rootFile);
    devSetup();
  }
  FakefsFile *rootFile = (FakefsFile *)rootDev.rootFile.firstObject;
  
  // Heap-allocate the name so it outlives the stack frame of the caller
  size_t len = strlength(name) + 1;
  char *name_persistent = (char *)malloc(len);
  memcpy(name_persistent, name, len);

  // S_IFBLK indicates a block device
  fakefsAddFile(&rootDev, rootFile, name_persistent, 0, S_IFBLK | S_IRUSR | S_IWUSR, handlers);
}

bool devMount(MountPoint *mount) {
  // install handlers
  mount->handlers = &fakefsHandlers;
  mount->stat = fakefsStat;
  mount->lstat = fakefsLstat;

  mount->fsInfo = malloc(sizeof(FakefsOverlay));
  memset(mount->fsInfo, 0, sizeof(FakefsOverlay));
  FakefsOverlay *dev = (FakefsOverlay *)mount->fsInfo;

  dev->fakefs = &rootDev;
  if (!rootDev.rootFile.firstObject) {
    fakefsSetupRoot(&rootDev.rootFile);
    devSetup();
  }

  return true;
}