#include <ahci.h>
#include <apic.h>
#include <bootloader.h>
#include <disk.h>
#include <isr.h>
#include <linked_list.h>
#include <malloc.h>
#include <paging.h>
#include <pmm.h>
#include <string.h>
#include <system.h>
#include <timer.h>
#include <util.h>
#include <vmm.h>
#include <dev.h>

#define BLKROGET      0x0000125e
#define BLKRRPART     0x0000125f
#define BLKGETSIZE    0x00001212
#define BLKSSZGET     0x00001268
#define BLKBSZGET     0x80041270
#define BLKGETSIZE64  0x80081272

#define ATA_CMD_FLUSH_CACHE 0xE7

static int ahciDiskCounter = 0;

// Forward declarations
size_t ahciDiskRead(OpenFile *fd, uint8_t *out, size_t limit);
size_t ahciDiskWrite(OpenFile *fd, uint8_t *out, size_t limit);
size_t ahciPartitionRead(OpenFile *fd, uint8_t *out, size_t limit);
size_t ahciPartitionWrite(OpenFile *fd, uint8_t *out, size_t limit);
size_t ahciDiskOpen(char *filename, int flags, int mode, OpenFile *fd, char **symlinkResolve);
bool ahciDiskClose(OpenFile *fd);
void ahciParsePartitions(void *diskFilePtr);
force_inline bool ahciPortReady(HBA_PORT *port);
bool ahciRead(ahci *ahciPtr, uint32_t portId, HBA_PORT *port, uint32_t startl, uint32_t starth, uint32_t count, uint8_t *buff);
bool ahciWrite(ahci *ahciPtr, uint32_t portId, HBA_PORT *port, uint32_t startl, uint32_t starth, uint32_t count, uint8_t *buff);
bool ahciFlushCache(ahci *ahciPtr, uint32_t portId, HBA_PORT *port);

// Generic AHCI driver (tested on real hardware)
const AHCI_DEVICE *isAHCIcontroller(PCIdevice *device) {
  for (int i = 0; i < (sizeof(ahci_ids) / sizeof(ahci_ids[0])); i++) {
    if (COMBINE_WORD(device->device_id, device->vendor_id) == ahci_ids[i].id)
      return &ahci_ids[i];
  }
  return 0;
}

/* Port command engine: */

void ahciCmdStart(HBA_PORT *port) {
  while (port->cmd & HBA_PxCMD_CR)
    ;
  port->cmd |= HBA_PxCMD_FRE;
  port->cmd |= HBA_PxCMD_ST;
}

void ahciCmdStop(HBA_PORT *port) {
  port->cmd &= ~HBA_PxCMD_ST;
  port->cmd &= ~HBA_PxCMD_FRE;
  while (1) {
    if (port->cmd & HBA_PxCMD_FR)
      continue;
    if (port->cmd & HBA_PxCMD_CR)
      continue;
    break;
  }
}

/* Command port operations: */

Spinlock LOCK_AHCI_CMD_FIND = ATOMIC_FLAG_INIT;
int      ahciCmdFind(ahci *ahciPtr, HBA_PORT *port) {
  spinlockAcquire(&LOCK_AHCI_CMD_FIND);

  int ret = -1;
  uint64_t start = timerTicks;
  while (ret == -1 && timerTicks <= (start + 500)) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
      if ((slots & 1) == 0 && !(ahciPtr->cmdSlotsPreping & (1 << i))) {
        ret = i;
        break;
      }
      slots >>= 1;
    }
  }

  if (ret == -1)
    printf("Cannot find free command list entry\n");
  else
    ahciPtr->cmdSlotsPreping |= (1 << ret);
  spinlockRelease(&LOCK_AHCI_CMD_FIND);
  return ret;
}

force_inline bool ahciCmdIssue(ahci *ahciPtr, HBA_PORT *port, int slot) {
  port->is = (uint32_t)-1; // Clear any pending port interrupts before issue
  port->ci = 1 << slot;
  ahciPtr->cmdSlotsPreping &= ~(1 << slot);

  uint64_t start = timerTicks;
  while (port->ci & (1 << slot)) {
    // Check for Task File Error Status (TFES) or Device Error (ERR bit in tfd)
    if ((port->is & HBA_PxIS_TFES) || (port->tfd & 0x01)) {
      debugf("[pci::ahci] AHCI Task File Error on slot %d! port->is: 0x%x, port->tfd: 0x%x, port->serr: 0x%x\n", 
             slot, port->is, port->tfd, port->serr);
      break;
    }

    if (timerTicks >= (start + 2000)) {
      debugf("[pci::ahci] CRITICAL: AHCI command timeout on slot %d! port->ci: 0x%x, port->tfd: 0x%x, port->is: 0x%x\n", 
             slot, port->ci, port->tfd, port->is);
      break;
    }
  }

  // If the command is still active (timed out or errored), safely recover the port
  if (port->ci & (1 << slot) || (port->is & HBA_PxIS_TFES) || (port->tfd & 0x01)) {
    // 1. Stop command engine and frame receiver cleanly
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    // Wait for CR (Command Running) to clear safely using a CPU pause loop
    int timeout = 100000;
    while ((port->cmd & HBA_PxCMD_CR) && timeout--) {
      __asm__ __volatile__("pause");
    }

    // 2. Clear error and interrupt status registers
    port->is = (uint32_t)-1;
    port->serr = port->serr; // Write-1-to-clear error bits

    // 3. Force-reset active slot registers to unblock queue
    port->ci = 0;
    port->sact = 0;

    // 4. Restart frame receiver and command engine
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;

    ahciPtr->cmdSlotsPreping &= ~(1 << slot);
    return false;
  }

  ahciPtr->cmdSlotsPreping &= ~(1 << slot);
  return true;
}

/* Set up AHCI parts for reading/writing: */

force_inline HBA_CMD_TBL *ahciSetUpCmd(ahci *ahciPtr, uint32_t portId,
                                       uint32_t cmdslot, uint16_t *buff,
                                       uint32_t count, bool write) {
  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)ahciPtr->clbVirt[portId];
  cmdheader = &cmdheader[cmdslot];
  memset(cmdheader, 0, sizeof(HBA_CMD_HEADER));

  size_t ctbaPhysCurr =
      (size_t)VirtualToPhysical((size_t)ahciPtr->ctbaVirt[portId]) +
      cmdslot * AHCI_MEM_TABLE;
  cmdheader->ctba = SPLIT_64_LOWER(ctbaPhysCurr);
  cmdheader->ctbau = SPLIT_64_HIGHER(ctbaPhysCurr);
  cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
  cmdheader->w = (uint8_t)write;
  cmdheader->prdtl = 0;

  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)((size_t)ahciPtr->ctbaVirt[portId] +
                                        cmdslot * AHCI_MEM_TABLE);
  memset(cmdtbl, 0,
         sizeof(HBA_CMD_TBL) + cmdheader->prdtl * sizeof(HBA_PRDT_ENTRY));

  size_t totalBytes = count << 9;
  size_t i = 0;
  if (totalBytes > 0 && buff != NULL) {
    while (totalBytes > 0) {
      size_t currVirt = (size_t)buff;
      size_t pageOffset = currVirt & 0xFFF;
      size_t spaceToPageEnd = 0x1000 - pageOffset;

      size_t spaceCovered = totalBytes;
      if (spaceCovered > spaceToPageEnd) {
        spaceCovered = spaceToPageEnd;
      }
      if (spaceCovered > AHCI_BYTES_PER_PRDT) {
        spaceCovered = AHCI_BYTES_PER_PRDT;
      }

      size_t targPhys = VirtualToPhysical(currVirt);
      cmdtbl->prdt_entry[i].dba = SPLIT_64_LOWER(targPhys);
      cmdtbl->prdt_entry[i].dbau = SPLIT_64_HIGHER(targPhys);
      cmdtbl->prdt_entry[i].dbc = spaceCovered - 1; // Zero-based byte count
      cmdtbl->prdt_entry[i].i = 1;

      buff += spaceCovered / 2;
      totalBytes -= spaceCovered;
      i++;

      if (i >= AHCI_PRDTS) {
        debugf("[ahci] FATAL! PRDT overflow, i{%ld} exceeds AHCI_PRDTS{%ld}!\n",
               i, (size_t)AHCI_PRDTS);
        panic();
      }
    }
  }

  cmdheader->prdtl = i;
  return cmdtbl;
}

int ahciPortType(HBA_PORT *port) {
  uint32_t ssts = port->ssts;
  uint8_t ipm = (ssts >> 8) & 0x0F;
  uint8_t det = ssts & 0x0F;

  if (det != HBA_PORT_DET_PRESENT)
    return AHCI_DEV_NULL;
  if (ipm != HBA_PORT_IPM_ACTIVE)
    return AHCI_DEV_NULL;

  switch (port->sig) {
  case SATA_SIG_ATAPI:
    return AHCI_DEV_SATAPI;
  case SATA_SIG_SEMB:
    return AHCI_DEV_SEMB;
  case SATA_SIG_PM:
    return AHCI_DEV_PM;
  default:
    return AHCI_DEV_SATA;
  }
}

force_inline bool ahciPortReady(HBA_PORT *port) {
  uint64_t start = timerTicks;
  while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ))) {
    if (timerTicks >= (start + 1000)) {
      printf("[pci::ahci] Port is hung ATA_DEV_BUSY{%d} ATA_DEV_DRQ{%d}\n",
             port->tfd & ATA_DEV_BUSY, port->tfd & ATA_DEV_DRQ);
      return false;
    }
  }
  return true;
}

void ahciPortRebase(ahci *ahciPtr, HBA_PORT *port, int portno) {
  ahciCmdStop(port);
  port->ie = 1;

  uint32_t clbPages = DivRoundUp(sizeof(HBA_CMD_HEADER) * 32, BLOCK_SIZE);
  void    *clbVirt = VirtualAllocate(clbPages);
  size_t   clbPhys = VirtualToPhysical((size_t)clbVirt);
  memset(clbVirt, 0, clbPages * BLOCK_SIZE);

  port->clb = SPLIT_64_LOWER(clbPhys);
  port->clbu = SPLIT_64_HIGHER(clbPhys);
  ahciPtr->clbVirt[portno] = clbVirt;

  void  *fbVirt = VirtualAllocate(DivRoundUp(256 * 32, BLOCK_SIZE));
  size_t fbPhys = VirtualToPhysical((size_t)fbVirt);
  memset((void *)(fbVirt), 0, 256 * 32);

  port->fb = SPLIT_64_LOWER(fbPhys);
  port->fbu = SPLIT_64_HIGHER(fbPhys);

  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)clbVirt;
  void  *ctbaVirt = VirtualAllocate(DivRoundUp(AHCI_MEM_ALL_TABLES, PAGE_SIZE));
  size_t ctbaPhys = (size_t)VirtualToPhysical((size_t)ctbaVirt);
  memset(ctbaVirt, 0, AHCI_MEM_ALL_TABLES);

  for (int i = 0; i < 32; i++) {
    cmdheader[i].prdtl = AHCI_PRDTS;
    size_t ctbaPhysCurr = ctbaPhys + i * AHCI_MEM_TABLE;
    cmdheader[i].ctba = SPLIT_64_LOWER(ctbaPhysCurr);
    cmdheader[i].ctbau = SPLIT_64_HIGHER(ctbaPhysCurr);
  }
  ahciPtr->ctbaVirt[portno] = ctbaVirt;

  if (port->serr & (1 << 10))
    port->serr |= (1 << 10);

  port->cmd |= (1 << 3);
  uint64_t targ = timerTicks + 150;
  while (port->cmd & (1 << 3) && timerTicks < targ)
    ;

  port->serr = port->serr;
  ahciCmdStart(port);
  ahciPtr->sata |= (1 << portno);
}

// Partition VFS Handler Context Structure
typedef struct {
  VfsHandlers handlers;
  void        *parentDisk;
  uint64_t     startLba;
  uint64_t     sectorCount;
} AhciPartitionFile;

// Per-disk VFS Handler Context Structure with tracking array for partitions
typedef struct {
  VfsHandlers handlers;
  uint32_t    portId;
  ahci       *ahciPtr;
  uint8_t    *dmaBuffer;
  uint64_t    sectorCount;
  bool        modified;
  void       *partitions[16]; // sdX1 to sdX16
} AhciDiskFile;

size_t ahciDiskRead(OpenFile *fd, uint8_t *out, size_t limit) {
  AhciDiskFile *disk = (AhciDiskFile *)fd->handlers;
  HBA_PORT *port = &disk->ahciPtr->mem->ports[disk->portId];

  size_t read_bytes = 0;
  while (read_bytes < limit) {
    uint32_t cur_lba = (fd->pointer + read_bytes) / 512;
    uint32_t cur_off = (fd->pointer + read_bytes) % 512;
    uint32_t chunk = 512 - cur_off;
    if (chunk > limit - read_bytes) 
      chunk = limit - read_bytes;

    // Validate user-space pointers (addresses below higher-half kernel base)
    if ((uintptr_t)(out + read_bytes) < 0xffff800000000000ULL) {
      if (VirtualToPhysical((size_t)(out + read_bytes)) == 0) {
        debugf("[pci::ahci] ERROR: ahciDiskRead unmapped user pointer %p\n", out + read_bytes);
        return ERR(EFAULT);
      }
    }

    if (!ahciRead(disk->ahciPtr, disk->portId, port, cur_lba, 0, 1, disk->dmaBuffer)) {
      return ERR(EIO);
    }
    memcpy(out + read_bytes, disk->dmaBuffer + cur_off, chunk);
    read_bytes += chunk;
  }
  
  fd->pointer += read_bytes;
  return read_bytes;
}

size_t ahciDiskWrite(OpenFile *fd, uint8_t *out, size_t limit) {
  AhciDiskFile *disk = (AhciDiskFile *)fd->handlers;
  HBA_PORT *port = &disk->ahciPtr->mem->ports[disk->portId];

  size_t written_bytes = 0;
  while (written_bytes < limit) {
    uint32_t cur_lba = (fd->pointer + written_bytes) / 512;
    uint32_t cur_off = (fd->pointer + written_bytes) % 512;
    uint32_t chunk = 512 - cur_off;
    if (chunk > limit - written_bytes) 
      chunk = limit - written_bytes;

    if ((uintptr_t)(out + written_bytes) < 0xffff800000000000ULL) {
      if (VirtualToPhysical((size_t)(out + written_bytes)) == 0) {
        return ERR(EFAULT);
      }
    }

    // Flag that the partition table area was touched
    if (cur_lba <= 33) {
      disk->modified = true;
    }

    if (cur_off > 0 || chunk < 512) {
      if (!ahciRead(disk->ahciPtr, disk->portId, port, cur_lba, 0, 1, disk->dmaBuffer)) {
        return ERR(EIO);
      }
    }
    memcpy(disk->dmaBuffer + cur_off, out + written_bytes, chunk);
    
    if (!ahciWrite(disk->ahciPtr, disk->portId, port, cur_lba, 0, 1, disk->dmaBuffer)) {
      return ERR(EIO);
    }
    written_bytes += chunk;
  }
  
  fd->pointer += written_bytes;
  return written_bytes;
}

size_t ahciPartitionRead(OpenFile *fd, uint8_t *out, size_t limit) {
  AhciPartitionFile *part = (AhciPartitionFile *)fd->handlers;
  if (fd->pointer >= (part->sectorCount * 512))
    return 0;
  if (fd->pointer + limit > (part->sectorCount * 512))
    limit = (part->sectorCount * 512) - fd->pointer;

  size_t oldPointer = fd->pointer;
  VfsHandlers *oldHandlers = fd->handlers;

  fd->handlers = (VfsHandlers *)part->parentDisk;
  fd->pointer = (part->startLba * 512) + oldPointer;

  size_t ret = ahciDiskRead(fd, out, limit);

  fd->handlers = oldHandlers;
  fd->pointer = oldPointer;
  return ret;
}

size_t ahciPartitionWrite(OpenFile *fd, uint8_t *out, size_t limit) {
  AhciPartitionFile *part = (AhciPartitionFile *)fd->handlers;
  if (fd->pointer >= (part->sectorCount * 512))
    return 0;
  if (fd->pointer + limit > (part->sectorCount * 512))
    limit = (part->sectorCount * 512) - fd->pointer;

  size_t oldPointer = fd->pointer;
  VfsHandlers *oldHandlers = fd->handlers;

  fd->handlers = (VfsHandlers *)part->parentDisk;
  fd->pointer = (part->startLba * 512) + oldPointer;

  size_t ret = ahciDiskWrite(fd, out, limit);

  fd->handlers = oldHandlers;
  fd->pointer = oldPointer;
  return ret;
}

size_t ahciPartitionIoctl(OpenFile *fd, unsigned long request, void *arg) {
  AhciPartitionFile *part = (AhciPartitionFile *)fd->handlers;
  uint64_t disk_bytes = part->sectorCount * 512ULL;
  switch (request) {
  case BLKGETSIZE64:
    if (!arg) return ERR(EFAULT);
    *(uint64_t *)arg = disk_bytes;
    return 0;
  case BLKGETSIZE:
    if (!arg) return ERR(EFAULT);
    *(unsigned long *)arg = (unsigned long)part->sectorCount;
    return 0;
  case BLKSSZGET:
  case BLKBSZGET:
    if (!arg) return ERR(EFAULT);
    *(int *)arg = 512;
    return 0;
  case BLKROGET:
    if (!arg) return ERR(EFAULT);
    *(int *)arg = 0;
    return 0;
  default:
    return ERR(ENOTTY);
  }
}

size_t ahciDiskOpen(char *filename, int flags, int mode, OpenFile *fd, char **symlinkResolve) {
  (void)filename;
  (void)flags;
  (void)mode;
  (void)fd;
  (void)symlinkResolve;
  return 0;
}

bool ahciDiskClose(OpenFile *fd) {
  // Ensure we are closing a disk file, not a partition file
  if (fd && fd->handlers && fd->handlers->read == ahciDiskRead) {
    AhciDiskFile *disk = (AhciDiskFile *)fd->handlers;
    if (disk->modified) {
      disk->modified = false;
      HBA_PORT *port = &disk->ahciPtr->mem->ports[disk->portId];
      ahciPortReady(port); // Wait for writes to finish flushing before rescan
      ahciFlushCache(disk->ahciPtr, disk->portId, port); // Ensure disk cache is committed
      port->is = (uint32_t)-1;
      port->serr = port->serr;
      port->ci = 0;
      port->sact = 0;
      debugf("[pci::ahci] Disk sd%c closed after modification, auto-rescanning partitions...\n", 'a' + disk->portId);
      ahciParsePartitions(disk);
    }
  }
  return true;
}

// Robust GPT-first partition parser with in-place partition updates
void ahciParsePartitions(void *diskFilePtr) {
  AhciDiskFile *disk = (AhciDiskFile *)diskFilePtr;
  HBA_PORT *port = &disk->ahciPtr->mem->ports[disk->portId];
  uint8_t *secBuf = disk->dmaBuffer;

  debugf("[pci::ahci] Parsing partitions for disk sd%c...\n", 'a' + disk->portId);

  // 1. Try GPT first (LBA 1)
  if (ahciRead(disk->ahciPtr, disk->portId, port, 1, 0, 1, secBuf)) {
    if (memcmp(secBuf, "EFI PART", 8) == 0) {
      debugf("[pci::ahci] Found GPT disk signature on sd%c\n", 'a' + disk->portId);

      uint64_t partitionEntriesLba = *(uint64_t *)(secBuf + 72);
      uint32_t numEntries = *(uint32_t *)(secBuf + 80);
      uint32_t sizeOfEntry = *(uint32_t *)(secBuf + 84);

      if (numEntries == 0 || sizeOfEntry == 0) {
        debugf("[pci::ahci] GPT header has invalid entries info (num: %u, size: %u)\n", numEntries, sizeOfEntry);
        return;
      }

      uint32_t totalBytes = numEntries * sizeOfEntry;
      uint32_t sectorsToRead = DivRoundUp(totalBytes, 512);
      uint8_t *entriesBuf = (uint8_t *)VirtualAllocate(DivRoundUp(totalBytes, PAGE_SIZE));
      if (!entriesBuf) {
        debugf("[pci::ahci] Failed to allocate memory for GPT partition entries\n");
        return;
      }

      for (uint32_t s = 0; s < sectorsToRead; s++) {
        if (!ahciRead(disk->ahciPtr, disk->portId, port, partitionEntriesLba + s, 0, 1, secBuf)) {
          debugf("[pci::ahci] Failed to read GPT partition entry sector %lu\n", partitionEntriesLba + s);
          break;
        }
        memcpy(entriesBuf + (s * 512), secBuf, 512);
      }

      int partIndex = 1;
      for (uint32_t i = 0; i < numEntries && partIndex <= 16; i++) {
        uint8_t *entry = entriesBuf + (i * sizeOfEntry);
        uint64_t *guidPart1 = (uint64_t *)entry;
        uint64_t *guidPart2 = (uint64_t *)(entry + 8);
        if (*guidPart1 == 0 && *guidPart2 == 0)
          continue; // Unused partition entry

        uint64_t startLba = *(uint64_t *)(entry + 32);
        uint64_t endLba = *(uint64_t *)(entry + 40);
        uint64_t pSectors = (endLba >= startLba) ? (endLba - startLba + 1) : 0;

        if (pSectors == 0)
          continue;

        char partName[32];
        snprintf(partName, sizeof(partName), "sd%c%d", 'a' + disk->portId, partIndex);

        AhciPartitionFile *partFile = (AhciPartitionFile *)disk->partitions[partIndex - 1];
        if (partFile) {
          // Update in place if already registered (e.g., on rescan)
          partFile->startLba = startLba;
          partFile->sectorCount = pSectors;
          debugf("[pci::ahci] Updated GPT partition %s (Start LBA: %lu, Sectors: %lu)\n", partName, startLba, pSectors);
        } else {
          // Register for the first time
          partFile = (AhciPartitionFile *)malloc(sizeof(AhciPartitionFile));
          partFile->handlers.read = ahciPartitionRead;
          partFile->handlers.write = ahciPartitionWrite;
          partFile->handlers.ioctl = ahciPartitionIoctl;
          partFile->handlers.open = ahciDiskOpen;
          partFile->handlers.close = ahciDiskClose;
          partFile->handlers.seek = fsSimpleSeek;
          partFile->handlers.stat = fakefsFstat;
          partFile->parentDisk = disk;
          partFile->startLba = startLba;
          partFile->sectorCount = pSectors;

          devRegisterBlockDevice(partName, (VfsHandlers *)partFile);
          disk->partitions[partIndex - 1] = partFile;
          debugf("[pci::ahci] Registered GPT partition %s (Start LBA: %lu, Sectors: %lu)\n", partName, startLba, pSectors);
        }
        partIndex++;
      }
      return;
    }
  }

  // 2. Fall back to MBR (LBA 0)
  if (ahciRead(disk->ahciPtr, disk->portId, port, 0, 0, 1, secBuf)) {
    if (secBuf[510] == 0x55 && secBuf[511] == 0xAA) {
      
      // CRITICAL FIX: Check if the first entry is a GPT protective MBR (type 0xEE)
      uint8_t *firstEntry = secBuf + 446;
      if (firstEntry[4] == 0xEE) {
        debugf("[pci::ahci] Skipping MBR fallback: found GPT protective MBR signature on sd%c\n", 'a' + disk->portId);
        return; // Do not parse protective MBR as a real partition table!
      }

      debugf("[pci::ahci] Found MBR disk signature on sd%c\n", 'a' + disk->portId);

      int partIndex = 1;
      for (int i = 0; i < 4 && partIndex <= 16; i++) {
        uint8_t *entry = secBuf + 446 + (i * 16);
        uint8_t type = entry[4];
        if (type == 0x00 || type == 0x05 || type == 0x0F)
          continue; // Unused or extended partition

        uint32_t startLba = *(uint32_t *)(entry + 8);
        uint32_t pSectors = *(uint32_t *)(entry + 12);

        if (pSectors == 0)
          continue;

        char partName[32];
        snprintf(partName, sizeof(partName), "sd%c%d", 'a' + disk->portId, partIndex);

        AhciPartitionFile *partFile = (AhciPartitionFile *)disk->partitions[partIndex - 1];
        if (partFile) {
          partFile->startLba = (uint64_t)startLba;
          partFile->sectorCount = (uint64_t)pSectors;
          debugf("[pci::ahci] Updated MBR partition %s (Start LBA: %u, Sectors: %u)\n", partName, startLba, pSectors);
        } else {
          partFile = (AhciPartitionFile *)malloc(sizeof(AhciPartitionFile));
          partFile->handlers.read = ahciPartitionRead;
          partFile->handlers.write = ahciPartitionWrite;
          partFile->handlers.ioctl = ahciPartitionIoctl;
          partFile->handlers.open = ahciDiskOpen;
          partFile->handlers.close = ahciDiskClose;
          partFile->handlers.seek = fsSimpleSeek;
          partFile->handlers.stat = fakefsFstat;
          partFile->parentDisk = disk;
          partFile->startLba = (uint64_t)startLba;
          partFile->sectorCount = (uint64_t)pSectors;

          devRegisterBlockDevice(partName, (VfsHandlers *)partFile);
          disk->partitions[partIndex - 1] = partFile;
          debugf("[pci::ahci] Registered MBR partition %s (Start LBA: %u, Sectors: %u)\n", partName, startLba, pSectors);
        }
        partIndex++;
      }
    } else {
      debugf("[pci::ahci] No valid MBR or GPT signature found on sd%c\n", 'a' + disk->portId);
    }
  }
}

size_t ahciDiskIoctl(OpenFile *fd, unsigned long request, void *arg) {
  AhciDiskFile *disk = (AhciDiskFile *)fd->handlers;
  if (!disk) return ERR(EINVAL);
  
  uint64_t total_sectors = disk->sectorCount ? disk->sectorCount : 20971520ULL;
  uint64_t disk_bytes = total_sectors * 512ULL;

  switch (request) {
  case BLKGETSIZE64: {
    if (!arg) return ERR(EFAULT);
    uint64_t *size_ptr = (uint64_t *)arg;
    *size_ptr = disk_bytes;
    return 0;
  }
  case BLKGETSIZE: {
    if (!arg) return ERR(EFAULT);
    unsigned long *sectors_ptr = (unsigned long *)arg;
    *sectors_ptr = (unsigned long)total_sectors;
    return 0;
  }
  case BLKSSZGET:
  case BLKBSZGET: {
    if (!arg) return ERR(EFAULT);
    int *ssz_ptr = (int *)arg;
    *ssz_ptr = 512;
    return 0;
  }
  case BLKROGET: {
    if (!arg) return ERR(EFAULT);
    int *ro_ptr = (int *)arg;
    *ro_ptr = 0;
    return 0;
  }
  case BLKRRPART: {
    debugf("[pci::ahci] BLKRRPART ioctl received on sd%c, rescanning partitions...\n", 'a' + disk->portId);
    HBA_PORT *port = &disk->ahciPtr->mem->ports[disk->portId];
    port->is = (uint32_t)-1;
    port->serr = port->serr;
    port->ci = 0;
    port->sact = 0;
    ahciParsePartitions(disk);
    return 0;
  }
  default:
    return ERR(ENOTTY);
  }
}

uint64_t ahciGetDiskSize(ahci *ahciPtr, uint32_t portId, HBA_PORT *port, uint8_t *dmaBuffer) {
  port->is = (uint32_t)-1;
  int slot = ahciCmdFind(ahciPtr, port);
  if (slot == -1)
    return 20971520ULL;

  HBA_CMD_TBL *cmdtbl = ahciSetUpCmd(ahciPtr, portId, slot, (uint16_t *)dmaBuffer, 1, false);

  memset(&cmdtbl->cfis, 0, sizeof(FIS_REG_H2D));
  uint8_t *fis = (uint8_t *)(&cmdtbl->cfis);
  fis[0] = 0x27;
  fis[1] = 1 << 7;
  fis[2] = 0xEC;
  fis[7] = 1 << 6;

  if (!ahciPortReady(port) || !ahciCmdIssue(ahciPtr, port, slot))
    return 20971520ULL;

  uint16_t *id_data = (uint16_t *)dmaBuffer;

  bool lba48 = (id_data[83] & (1 << 10)) != 0;
  if (lba48) {
    uint64_t sectors = ((uint64_t)id_data[103] << 48) |
                       ((uint64_t)id_data[102] << 32) |
                       ((uint64_t)id_data[101] << 16) |
                       (uint64_t)id_data[100];
    if (sectors > 0)
      return sectors;
  }

  uint32_t sectors28 = ((uint32_t)id_data[61] << 16) | (uint32_t)id_data[60];
  if (sectors28 > 0)
    return (uint64_t)sectors28;

  return 20971520ULL;
}

void ahciPortProbe(ahci *ahciPtr, HBA_MEM *abar) {
  uint32_t pi = abar->pi;
  for (int i = 0; i < 32; i++) {
    if (pi & 1) {
      int dt = ahciPortType(&abar->ports[i]);
      if (dt == AHCI_DEV_SATA) {
        debugf("[pci::ahci] SATA drive found at port %d\n", i);
        ahciPortRebase(ahciPtr, &abar->ports[i], i);

        char diskName[16];
        snprintf(diskName, sizeof(diskName), "sd%c", 'a' + ahciDiskCounter);
        ahciDiskCounter++;

        AhciDiskFile *diskFile = (AhciDiskFile *)malloc(sizeof(AhciDiskFile));
        memset(diskFile, 0, sizeof(AhciDiskFile)); // Zeros out partition tracking array
        diskFile->handlers.read = ahciDiskRead;
        diskFile->handlers.write = ahciDiskWrite;
        diskFile->handlers.ioctl = ahciDiskIoctl;
        diskFile->handlers.open = ahciDiskOpen;
        diskFile->handlers.close = ahciDiskClose;
        diskFile->handlers.seek = fsSimpleSeek;
        diskFile->handlers.stat = fakefsFstat;
        diskFile->portId = i;
        diskFile->ahciPtr = ahciPtr;
        diskFile->dmaBuffer = (uint8_t *)VirtualAllocate(1);

        diskFile->sectorCount = ahciGetDiskSize(ahciPtr, i, &abar->ports[i], diskFile->dmaBuffer);
        debugf("[pci::ahci] Drive %s capacity: %lu sectors (~%lu MB)\n", 
               diskName, diskFile->sectorCount, (diskFile->sectorCount * 512) / (1024 * 1024));

        devRegisterBlockDevice(diskName, (VfsHandlers *)diskFile);

        ahciParsePartitions(diskFile);

      } else if (dt == AHCI_DEV_SATAPI) {
        debugf("[pci::ahci] (unsupported) SATAPI drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_SEMB) {
        debugf("[pci::ahci] (unsupported) SEMB drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_PM) {
        debugf("[pci::ahci] (unsupported) PM drive found at port %d\n", i);
      }
    }
    pi >>= 1;
  }
}

#define AHCI_MAX_SECTORS_PER_CMD 128 // Safe limit well within the 256 PRDT capacity

bool ahciRead(ahci *ahciPtr, uint32_t portId, HBA_PORT *port, uint32_t startl,
              uint32_t starth, uint32_t count, uint8_t *buff) {
  assert(((size_t)buff % 2) == 0);

  uint32_t sectorsRemaining = count;
  uint32_t currentStartL = startl;
  uint32_t currentStartH = starth;
  uint8_t *currentBuff = buff;

  while (sectorsRemaining > 0) {
    uint32_t chunkSectors = sectorsRemaining;
    if (chunkSectors > AHCI_MAX_SECTORS_PER_CMD) {
      chunkSectors = AHCI_MAX_SECTORS_PER_CMD;
    }

    port->is = (uint32_t)-1;
    int slot = ahciCmdFind(ahciPtr, port);
    if (slot == -1)
      return false;

    HBA_CMD_TBL *cmdtbl =
        ahciSetUpCmd(ahciPtr, portId, slot, (uint16_t *)currentBuff, chunkSectors, false);

    memset(&cmdtbl->cfis, 0, sizeof(FIS_REG_H2D));
    uint8_t *fis = (uint8_t *)(&cmdtbl->cfis);
    fis[0] = 0x27;
    fis[1] = 1 << 7;
    fis[2] = ATA_CMD_READ_DMA_EX;
    fis[3] = 0;
    fis[4] = (uint8_t)currentStartL;
    fis[5] = (uint8_t)(currentStartL >> 8);
    fis[6] = (uint8_t)(currentStartL >> 16);
    fis[7] = 1 << 6;
    fis[8] = (uint8_t)(currentStartL >> 24);
    fis[9] = (uint8_t)currentStartH;
    fis[10] = (uint8_t)(currentStartH >> 8);
    fis[11] = 0;
    fis[12] = chunkSectors & 0xFF;
    fis[13] = (chunkSectors >> 8) & 0xFF;
    fis[14] = 0;
    fis[15] = 0;

    if (!ahciPortReady(port))
      return false;

    if (!ahciCmdIssue(ahciPtr, port, slot))
      return false;

    // Advance pointers and counters for the next chunk
    sectorsRemaining -= chunkSectors;
    currentBuff += chunkSectors * 512;

    uint64_t fullLba = ((uint64_t)currentStartH << 32) | currentStartL;
    fullLba += chunkSectors;
    currentStartL = (uint32_t)(fullLba & 0xFFFFFFFF);
    currentStartH = (uint32_t)(fullLba >> 32);
  }

  return true;
}

bool ahciWrite(ahci *ahciPtr, uint32_t portId, HBA_PORT *port, uint32_t startl,
               uint32_t starth, uint32_t count, uint8_t *buff) {
  assert(((size_t)buff % 2) == 0);

  uint32_t sectorsRemaining = count;
  uint32_t currentStartL = startl;
  uint32_t currentStartH = starth;
  uint8_t *currentBuff = buff;

  while (sectorsRemaining > 0) {
    uint32_t chunkSectors = sectorsRemaining;
    if (chunkSectors > AHCI_MAX_SECTORS_PER_CMD) {
      chunkSectors = AHCI_MAX_SECTORS_PER_CMD;
    }

    port->is = (uint32_t)-1;
    int slot = ahciCmdFind(ahciPtr, port);
    if (slot == -1)
      return false;

    HBA_CMD_TBL *cmdtbl =
        ahciSetUpCmd(ahciPtr, portId, slot, (uint16_t *)currentBuff, chunkSectors, true);

    memset(&cmdtbl->cfis, 0, sizeof(FIS_REG_H2D));
    uint8_t *fis = (uint8_t *)(&cmdtbl->cfis);
    fis[0] = 0x27;
    fis[1] = 1 << 7;
    fis[2] = ATA_CMD_WRITE_DMA_EX;
    fis[3] = 0;
    fis[4] = (uint8_t)currentStartL;
    fis[5] = (uint8_t)(currentStartL >> 8);
    fis[6] = (uint8_t)(currentStartL >> 16);
    fis[7] = (uint8_t)1 << 6;
    fis[8] = (uint8_t)(currentStartL >> 24);
    fis[9] = (uint8_t)currentStartH;
    fis[10] = (uint8_t)(currentStartH >> 8);
    fis[11] = 0;
    fis[12] = chunkSectors & 0xFF;
    fis[13] = (chunkSectors >> 8) & 0xFF;
    fis[14] = 0;
    fis[15] = 0;

    if (!ahciPortReady(port))
      return false;

    if (!ahciCmdIssue(ahciPtr, port, slot))
      return false;

    sectorsRemaining -= chunkSectors;
    currentBuff += chunkSectors * 512;

    uint64_t fullLba = ((uint64_t)currentStartH << 32) | currentStartL;
    fullLba += chunkSectors;
    currentStartL = (uint32_t)(fullLba & 0xFFFFFFFF);
    currentStartH = (uint32_t)(fullLba >> 32);
  }

  return true;
}

bool ahciFlushCache(ahci *ahciPtr, uint32_t portId, HBA_PORT *port) {
  port->is = (uint32_t)-1;
  int slot = ahciCmdFind(ahciPtr, port);
  if (slot == -1)
    return false;

  HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)ahciPtr->clbVirt[portId];
  cmdheader = &cmdheader[slot];
  memset(cmdheader, 0, sizeof(HBA_CMD_HEADER));

  size_t ctbaPhysCurr =
      (size_t)VirtualToPhysical((size_t)ahciPtr->ctbaVirt[portId]) +
      slot * AHCI_MEM_TABLE;
  cmdheader->ctba = SPLIT_64_LOWER(ctbaPhysCurr);
  cmdheader->ctbau = SPLIT_64_HIGHER(ctbaPhysCurr);
  cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
  cmdheader->w = 0;
  cmdheader->prdtl = 0;

  HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)((size_t)ahciPtr->ctbaVirt[portId] +
                                        slot * AHCI_MEM_TABLE);
  memset(cmdtbl, 0, sizeof(HBA_CMD_TBL));

  memset(&cmdtbl->cfis, 0, sizeof(FIS_REG_H2D));
  uint8_t *fis = (uint8_t *)(&cmdtbl->cfis);
  fis[0] = 0x27;
  fis[1] = 1 << 7;
  fis[2] = ATA_CMD_FLUSH_CACHE;
  fis[3] = 0;

  if (!ahciPortReady(port))
    return false;

  return ahciCmdIssue(ahciPtr, port, slot);
}

void ahciInterruptHandler(AsmPassedInterrupt *regs) {
  (void)regs;
  PCI *browse = (PCI *)dsPCI.firstObject;
  while (browse) {
    if (browse->driver == PCI_DRIVER_AHCI) {
      ahci *ahciPtr = browse->extra;
      for (int portNum = 0; portNum < 32; portNum++) {
        if (!(ahciPtr->sata & (1 << portNum)))
          continue;

        HBA_PORT *port = &ahciPtr->mem->ports[portNum];
        if (port->is & HBA_PxIS_TFES) {
          port->is |= HBA_PxIS_TFES;
          port->serr = port->serr;
        }
        ahciPtr->mem->is = ahciPtr->mem->is;
        ahciPtr->mem->ports[portNum].is = ahciPtr->mem->ports[portNum].is;
      }
    }
    browse = (PCI *)browse->_ll.next;
  }
}

bool initiateAHCI(PCIdevice *device) {
  const AHCI_DEVICE *ahciDevice = isAHCIcontroller(device);
  if (!ahciDevice)
    return false;

  PCIgeneralDevice *details =
      (PCIgeneralDevice *)malloc(sizeof(PCIgeneralDevice));
  GetGeneralDevice(device, details);
  uint32_t base = details->bar[5] & 0xFFFFFFF0;

  uint32_t command_status = COMBINE_WORD(device->status, device->command);
  if (!(command_status & (1 << 2)))
    command_status |= (1 << 2);
  if (!(command_status & (1 << 1)))
    command_status |= (1 << 1);
  if (command_status & (1 << 10))
    command_status &= ~(1 << 10);
  ConfigWriteDword(device->bus, device->slot, device->function, PCI_COMMAND,
                   command_status);

  PCI *pci = lookupPCIdevice(device);
  setupPCIdeviceDriver(pci, PCI_DRIVER_AHCI, PCI_DRIVER_CATEGORY_STORAGE);

  ahci *ahciPtr = (ahci *)malloc(sizeof(ahci));
  memset(ahciPtr, 0, sizeof(ahci));
  pci->extra = ahciPtr;

  HBA_MEM *mem = (HBA_MEM *)(bootloader.hhdmOffset + base);

  ahciPtr->bsdInfo = ahciDevice;
  ahciPtr->mem = mem;
  uint32_t size = strlength(ahciDevice->name) + 1;
  pci->name = (char *)malloc(size);
  memcpy(pci->name, ahciDevice->name, size);

  mem->ghc |= (1 << 0);
  while (mem->ghc & (1 << 0))
    ;

  if (!(mem->bohc & 2) || mem->cap2 & AHCI_BIOS_OWNED) {
    mem->bohc = (mem->bohc & ~8) | 2;
    while ((mem->bohc & 1))
      ;
    if ((mem->bohc & 1)) {
      mem->bohc = 2;
      mem->bohc |= 8;
    }
  }

  if (!(mem->ghc & (1 << 31)))
    mem->ghc |= (1 << 31);

  ahciPortProbe(ahciPtr, mem);

  uint8_t targIrq = ioApicPciRegister(device, details);
  pci->irqHandler = registerIRQhandler(targIrq, &ahciInterruptHandler);
  if (!(mem->ghc & (1 << 1)))
    mem->ghc |= 1 << 1;

  return true;
}