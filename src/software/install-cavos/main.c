// cavOS in-VM installer.
//
// Ships with the OS (target/usr/bin/install-cavos) and lets you "install" the
// running system onto another virtual disk while booted from the live disk.
//
// The kernel exposes every SATA disk as a whole-disk block file (/dev/sda,
// /dev/sdb, ...) and each MBR/GPT partition as /dev/sdX1, /dev/sdX2, ...
// Since the kernel can parse a partition table but there is no in-guest mkfs,
// installation == cloning the running (boot) disk sector-for-sector onto the
// target, MBR + limine stage + FAT boot partition + ext2 root included.
// Closing the target and reopening it makes the kernel rescan/clone the
// partition table into /dev.
//
// Usage (from the live boot):
//     install-cavos /dev/sdb            # source = auto-detected boot disk
//     install-cavos /dev/sda /dev/sdb   # source + target explicitly
//     install-cavos -l                  # list disks + sizes
//
// After it finishes, shut the VM down and boot the machine off the target.

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 0x80081272 /* _IOR(0x12,114,size_t) */
#endif

#define SD_SECTOR_SIZE 512ULL
#define COPY_BUFFER_SIZE (1024ULL * 1024ULL) /* 1 MiB chunks */

typedef struct {
  uint8_t boot_flag;
  uint8_t chs_start[3];
  uint8_t type;
  uint8_t chs_end[3];
  uint32_t start_lba;
  uint32_t sectors;
} __attribute__((packed)) MbrEntry;

typedef struct {
  uint8_t code[446];
  MbrEntry entries[4];
  uint16_t signature;
} __attribute__((packed)) MasterBootRecord;

#define MBR_SIGNATURE 0xAA55

/* ------------------------------------------------------------------ */
/* output (bare write() so nothing hides in libc buffers)             */
/* ------------------------------------------------------------------ */

static void raw_out(int fd, const char *s) {
  size_t len = strlen(s);
  size_t done = 0;
  while (done < len) {
    ssize_t n = write(fd, s + done, len - done);
    if (n <= 0)
      return;
    done += (size_t)n;
  }
}

static void out(const char *s) { raw_out(1, s); }

static void out_err(const char *s) { raw_out(2, s); }

static void outf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out(buf);
}

static void outf_err(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out_err(buf);
}

/* ------------------------------------------------------------------ */
/* block device helpers                                               */
/* ------------------------------------------------------------------ */

static int blk_size(int fd, uint64_t *size_out) {
  uint64_t sz = 0;
  if (ioctl(fd, BLKGETSIZE64, &sz) != 0)
    return -1;
  *size_out = sz;
  return 0;
}

static bool is_whole_disk_name(const char *name) {
  size_t len = strlen(name);
  if (len < 3 || strncmp(name, "sd", 2) != 0)
    return false;
  /* whole disk ends in a letter; partition nodes end in a digit */
  return isalpha((unsigned char)name[len - 1]);
}

static bool read_bytes_at(int fd, uint64_t byte_off, void *dst, size_t len) {
  if (lseek(fd, (off_t)byte_off, SEEK_SET) < 0)
    return false;
  size_t done = 0;
  while (done < len) {
    ssize_t n = read(fd, (uint8_t *)dst + done, len - done);
    if (n <= 0)
      return false;
    done += (size_t)n;
  }
  return true;
}

static bool read_mbr(int fd, MasterBootRecord *mbr) {
  return read_bytes_at(fd, 0, mbr, sizeof(*mbr));
}

/* read the first sector of a partition */
static bool read_partition_sector(int fd, const MbrEntry *e, uint8_t *sec) {
  return read_bytes_at(fd, (uint64_t)e->start_lba * SD_SECTOR_SIZE, sec,
                       SD_SECTOR_SIZE);
}

static bool partition_is_fat32(const uint8_t *sec) {
  if (sec[510] != 0x55 || sec[511] != 0xAA)
    return false;
  return memcmp(sec + 82, "FAT32   ", 8) == 0;
}

static bool partition_is_ext2_cavos(const uint8_t *super) {
  /* superblock at byte 1024 inside the partition; magic at +56, label at +120 */
  if (super[0] != 0x53 || super[1] != 0xEF)
    return false;
  char label[17];
  memcpy(label, super + 120, 16);
  label[16] = '\0';
  return strncmp(label, "cavOS", 5) == 0;
}

/* Does the disk carry the OS (FAT "LIMINE" boot and/or ext2 "cavOS" root)? */
static bool disk_has_cavos(int fd) {
  MasterBootRecord mbr;
  if (!read_mbr(fd, &mbr))
    return false;
  if (mbr.signature != MBR_SIGNATURE)
    return false;

  for (int i = 0; i < 4; i++) {
    uint8_t type = mbr.entries[i].type;
    if (type == 0 || type == 0x05 || type == 0x0F || type == 0xEE)
      continue; /* unused / extended / GPT protective */
    if (mbr.entries[i].sectors == 0)
      continue;

    uint8_t sec[1024];
    if (read_partition_sector(fd, &mbr.entries[i], sec)) {
      if (partition_is_fat32(sec))
        return true;
    }
    if (read_bytes_at(fd, (uint64_t)mbr.entries[i].start_lba * SD_SECTOR_SIZE,
                      sec, 1024)) {
      if (partition_is_ext2_cavos(sec))
        return true;
    }
  }
  return false;
}

static int open_dev(const char *name, int flags) {
  char path[64];
  snprintf(path, sizeof(path), "/dev/%s", name);
  return open(path, flags);
}

/* ------------------------------------------------------------------ */
/* disk enumeration                                                   */
/* ------------------------------------------------------------------ */

static int find_boot_disk(char *out_name, size_t out_sz) {
  DIR *dir = opendir("/dev");
  if (!dir) {
    outf_err("installer: cannot open /dev\n");
    return -1;
  }

  struct dirent *dp;
  while ((dp = readdir(dir)) != NULL) {
    if (!is_whole_disk_name(dp->d_name))
      continue;
    int fd = open_dev(dp->d_name, O_RDONLY);
    if (fd < 0)
      continue;
    bool has = disk_has_cavos(fd);
    close(fd);
    if (has) {
      snprintf(out_name, out_sz, "%s", dp->d_name);
      closedir(dir);
      return 0;
    }
  }
  closedir(dir);
  return -1;
}

static void list_disks(void) {
  DIR *dir = opendir("/dev");
  if (!dir) {
    outf_err("installer: cannot open /dev\n");
    return;
  }

  out("Block devices:\n");
  struct dirent *dp;
  while ((dp = readdir(dir)) != NULL) {
    if (!is_whole_disk_name(dp->d_name))
      continue;

    int fd = open_dev(dp->d_name, O_RDONLY);
    if (fd < 0)
      continue;

    uint64_t sz = 0;
    bool have = blk_size(fd, &sz) == 0;

    bool boot = disk_has_cavos(fd);
    close(fd);

    if (!have)
      outf("  /dev/%-5s  <unknown size>\n", dp->d_name);
    else
      outf("  /dev/%-5s  %llu sectors  (~%llu MiB)%s\n", dp->d_name,
           (unsigned long long)(sz / SD_SECTOR_SIZE),
           (unsigned long long)(sz / (1024ULL * 1024ULL)),
           boot ? "  <-- running boot disk" : "");
  }
  closedir(dir);
}

/* ------------------------------------------------------------------ */
/* main flow                                                          */
/* ------------------------------------------------------------------ */

static bool confirm_destructive(const char *target_name, uint64_t src_bytes,
                                uint64_t dst_bytes) {
  outf("You are about to OVERWRITE ALL DATA on /dev/%s\n", target_name);
  outf("  source: %llu MiB   target: %llu MiB\n",
       (unsigned long long)(src_bytes / (1024ULL * 1024ULL)),
       (unsigned long long)(dst_bytes / (1024ULL * 1024ULL)));
  out("Type 'yes' to continue: ");

  char answer[8] = {0};
  size_t got = 0;
  while (got < sizeof(answer) - 1) {
    char c;
    ssize_t n = read(0, &c, 1);
    if (n <= 0)
      break;
    if (c == '\n' || c == '\r')
      break;
    answer[got++] = c;
  }
  out("\n");

  return strcmp(answer, "yes") == 0;
}

static int do_install(const char *src_name, const char *dst_name, bool skip_confirm) {
  int src = open_dev(src_name, O_RDONLY);
  if (src < 0) {
    outf_err("installer: cannot open /dev/%s\n", src_name);
    return 1;
  }
  int dst = open_dev(dst_name, O_RDWR);
  if (dst < 0) {
    outf_err("installer: cannot open /dev/%s for writing\n", dst_name);
    close(src);
    return 1;
  }

  uint64_t src_bytes = 0, dst_bytes = 0;
  if (blk_size(src, &src_bytes) != 0 || src_bytes == 0) {
    outf_err("installer: cannot determine size of /dev/%s\n", src_name);
    close(src);
    close(dst);
    return 1;
  }
  if (blk_size(dst, &dst_bytes) != 0) {
    outf_err("installer: cannot determine size of /dev/%s\n", dst_name);
    close(src);
    close(dst);
    return 1;
  }

  if (src_bytes > dst_bytes) {
    outf_err("installer: target /dev/%s (%llu MiB) is smaller than source "
             "/dev/%s (%llu MiB)\n",
             dst_name, (unsigned long long)(dst_bytes / (1024ULL * 1024ULL)),
             src_name, (unsigned long long)(src_bytes / (1024ULL * 1024ULL)));
    close(src);
    close(dst);
    return 1;
  }

  if (!skip_confirm) {
    if (!confirm_destructive(dst_name, src_bytes, dst_bytes)) {
      out("Aborted.\n");
      close(src);
      close(dst);
      return 1;
    }
  } else {
    outf("Skipping confirmation (-y): OVERWRITING ALL DATA on /dev/%s\n",
         dst_name);
  }

  uint8_t *buf = malloc(COPY_BUFFER_SIZE);
  if (!buf) {
    out_err("installer: out of memory\n");
    close(src);
    close(dst);
    return 1;
  }
  memset(buf, 0, COPY_BUFFER_SIZE); /* fault all pages in */

  outf("Installing: /dev/%s -> /dev/%s  (%llu MiB)\n", src_name, dst_name,
       (unsigned long long)(src_bytes / (1024ULL * 1024ULL)));

  uint64_t copied = 0;
  int last_pct = -100; /* force a first line */

  while (copied < src_bytes) {
    size_t want = (src_bytes - copied) < COPY_BUFFER_SIZE
                      ? (size_t)(src_bytes - copied)
                      : COPY_BUFFER_SIZE;

    ssize_t n = read(src, buf, want);
    if (n <= 0) {
      outf_err("installer: read failed on /dev/%s (offset %llu)\n", src_name,
               (unsigned long long)copied);
      close(src);
      close(dst);
      free(buf);
      return 1;
    }

    size_t off = 0;
    while ((size_t)off < (size_t)n) {
      ssize_t w = write(dst, buf + off, (size_t)n - off);
      if (w <= 0) {
        outf_err("installer: write failed on /dev/%s (offset %llu)\n", dst_name,
                 (unsigned long long)copied);
        close(src);
        close(dst);
        free(buf);
        return 1;
      }
      off += (size_t)w;
    }

    copied += (size_t)n;

    int pct = (int)((copied * 100ULL) / src_bytes);
    if (pct - last_pct >= 5 || copied == src_bytes) {
      outf("\r  %3d%%  (%llu MiB / %llu MiB)", pct,
           (unsigned long long)(copied / (1024ULL * 1024ULL)),
           (unsigned long long)(src_bytes / (1024ULL * 1024ULL)));
      if (copied == src_bytes)
        out("\n");
      last_pct = pct;
    }
  }

  close(src);
  close(dst); /* flush + partition rescan */
  free(buf);

  /* verify: reopen the target and check the MBR got written */
  int check = open_dev(dst_name, O_RDONLY);
  if (check < 0) {
    outf_err("installer: could not reopen /dev/%s to verify\n", dst_name);
    return 1;
  }
  MasterBootRecord mbr;
  if (!read_mbr(check, &mbr)) {
    outf_err("installer: verification read failed on /dev/%s\n", dst_name);
    close(check);
    return 1;
  }
  close(check);

  if (mbr.signature != MBR_SIGNATURE) {
    outf_err("installer: FAILED - target /dev/%s has no valid MBR after "
             "install!\n",
             dst_name);
    return 1;
  }

  out("\nInstall complete.\n");
  outf("MBR signature on /dev/%s verified (0x%04X).\n", dst_name,
       (unsigned)mbr.signature);
  out("Partitions written:\n");
  for (int i = 0; i < 4; i++) {
    if (mbr.entries[i].type == 0 || mbr.entries[i].type == 0x05 ||
        mbr.entries[i].type == 0x0F)
      continue;
    outf("  p%d  type 0x%02X  LBA %u  (%u sectors, %.1f MiB)\n", i + 1,
         (unsigned)mbr.entries[i].type,
         (unsigned)mbr.entries[i].start_lba,
         (unsigned)mbr.entries[i].sectors,
         (double)mbr.entries[i].sectors * SD_SECTOR_SIZE / (1024.0 * 1024.0));
  }
  out("\nDone. Shut the VM down, remove the live disk, and boot from /dev/"
      "sdX.\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    out("cavOS installer - installs the running system onto another disk.\n");
    out("  install-cavos -l                       list block devices\n");
    out("  install-cavos [-y] TARGET_DEVICE       install (source = boot disk)\n");
    out("  install-cavos [-y] SOURCE TARGET       install (explicit source)\n");
    out("  -y  skip the interactive confirmation\n");
    return 0;
  }

  if (argc == 2 && strcmp(argv[1], "-l") == 0) {
    list_disks();
    return 0;
  }

  char src_name[32];
  const char *dst_name;

  bool skip_confirm = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--yes") == 0) {
      skip_confirm = true;
      for (int j = i; j < argc - 1; j++)
        argv[j] = argv[j + 1];
      argc--;
      break;
    }
  }

  if (argc == 2) {
    dst_name = strstr(argv[1], "/dev/") ? argv[1] + 5 : argv[1];
    if (find_boot_disk(src_name, sizeof(src_name)) != 0) {
      out_err("installer: could not auto-detect the running boot disk "
              "(no partition with a cavOS root was found).\n");
      out_err("installer: pass the source explicitly: install-cavos "
              "SOURCE_DEVICE TARGET_DEVICE\n");
      return 1;
    }
    if (strcmp(src_name, dst_name) == 0) {
      outf_err("installer: refusing to install onto the running boot disk "
               "/dev/%s\n",
               dst_name);
      return 1;
    }
    return do_install(src_name, dst_name, skip_confirm);
  }

  if (argc == 3) {
    snprintf(src_name, sizeof(src_name), "%s",
             strstr(argv[1], "/dev/") ? argv[1] + 5 : argv[1]);
    dst_name = strstr(argv[2], "/dev/") ? argv[2] + 5 : argv[2];
    if (strcmp(src_name, dst_name) == 0) {
      outf_err("installer: source and target are the same device (/dev/%s)\n",
               dst_name);
      return 1;
    }
    return do_install(src_name, dst_name, skip_confirm);
  }

  outf("Usage: %s [-l | TARGET_DEVICE | SOURCE_DEVICE TARGET_DEVICE]\n",
       argv[0]);
  return 1;
}