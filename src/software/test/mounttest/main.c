// Exercises mount(2) failure paths. Every one of these used to be able to take
// the kernel down: fsMount's failure paths called fsUnmount, and fsUnmount
// called panic(). So a typo in a mount command, an unknown filesystem name or a
// bad device would halt the machine.
//
// Every case below is expected to FAIL. If the kernel panics, the process never
// reaches the final line and the boot log fills with a panic dump instead.

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

struct mountCase {
    const char   *source;
    const char   *target;
    const char   *type;
    unsigned long flags;
};

int main(void) {
    static const struct mountCase cases[] = {
        // wrong filesystem for what's actually on the partition
        {"/dev/sda2", "/mnt/x", "vfat", 0},
        // partition that cannot exist
        {"/dev/sda9", "/mnt/x", "ext4", 0},
        // not a block device at all
        {"/etc/hostname", "/mnt/x", "ext4", 0},
        // filesystem cavOS has no provider for
        {"/dev/sda2", "/mnt/x", "nosuchfs", 0},
        // mount rearrangements we deliberately refuse
        {"/dev/sda2", "/mnt/x", "ext4", MS_BIND},
        {"/dev/sda2", "/mnt/x", "ext4", MS_MOVE},
        {"/dev/sda2", "/mnt/x", "ext4", MS_REMOUNT},
        // target must be absolute
        {"proc", "relative/path", "proc", 0},
        // NULL target and empty target
        {"proc", NULL, "proc", 0},
        {"proc", "", "proc", 0},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        errno = 0;
        int r = mount(cases[i].source, cases[i].target, cases[i].type,
                      cases[i].flags, NULL);
        printf("mount(%-14s %-13s %-9s 0x%04lx) = %2d errno=%2d %-8s\n",
               cases[i].source, cases[i].target, cases[i].type, cases[i].flags,
               r, r ? errno : 0, r ? strerror(errno) : "(mounted)");
        fflush(stdout);
    }

    // The kernel must still be healthy: /proc has to still be readable, which
    // means the failed mounts above neither unmounted it nor corrupted the
    // mount table they were rejected by.
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) {
        printf("FAIL: /proc/uptime unreadable after failed mounts\n");
        return 1;
    }
    double up = 0;
    if (fscanf(f, "%lf", &up) != 1) {
        fclose(f);
        printf("FAIL: could not parse /proc/uptime\n");
        return 1;
    }
    fclose(f);
    printf("OK: survived %zu failed mounts, uptime=%.2f\n",
           sizeof(cases) / sizeof(cases[0]), up);

    // And the happy path: sda1 is the FAT boot partition, so this one is
    // expected to actually mount. If validation were rejecting valid requests
    // the failure tests above would still pass.
    errno = 0;
    int ok = mount("/dev/sda1", "/mnt/bootfs", "vfat", 0, NULL);
    printf("mount(/dev/sda1 /mnt/bootfs vfat) = %d errno=%d %s\n", ok,
           ok ? errno : 0, ok ? "mounted" : strerror(errno));

    // vfat and fat32 name the same on-disk format, so both must be accepted.
    errno = 0;
    int ok2 = mount("/dev/sda1", "/mnt/bootfs2", "fat32", 0, NULL);
    printf("mount(/dev/sda1 /mnt/bootfs2 fat32) = %d errno=%d %s\n", ok2,
           ok2 ? errno : 0, ok2 ? "mounted" : strerror(errno));

    // ext2/ext3 share the ext2 layout, so both must be accepted too.
    errno = 0;
    int ok3 = mount("/dev/sda2", "/mnt/rootfs", "ext3", 0, NULL);
    printf("mount(/dev/sda2 /mnt/rootfs ext3) = %d errno=%d %s\n", ok3,
           ok3 ? errno : 0, ok3 ? "mounted" : strerror(errno));

    return 0;
}