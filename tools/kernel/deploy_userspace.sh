#!/bin/sh
# Deploy-only helper: push host-built userspace binaries into the existing
# disk.img *without* needing sudo or a loop mount.
#
# target/lib and target/usr are root-owned, so `bmake disk` needs sudo to
# refresh them. When iterating on a kernel/userspace bug this splices files
# straight into the ext4 partition instead, editing the partition image with
# debugfs and writing it back with dd - neither of which need privileges.
#
# Usage: tools/kernel/deploy_userspace.sh src1:dst1 [src2:dst2 ...]
set -e

SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
TOP=$(realpath "${SCRIPTPATH}/../..")
IMG=${DEPLOY_IMG:-${TOP}/disk.img}
WORK=$(mktemp -d)
trap 'rm -rf "${WORK}"' EXIT

[ "$#" -gt 0 ] || { echo "usage: $0 src:dst [src:dst ...]" >&2; exit 1; }

# Partition geometry, read from the image rather than hardcoded. cavOS uses a
# plain MBR table; partition 1 is the FAT boot partition Limine reads and
# partition 2 is the ext4 root.
eval "$(sfdisk -l -o Start,Sectors --json "${IMG}" |
    python3 -c '
import json, sys
parts = json.load(sys.stdin)["partitiontable"]["partitions"]
parts.sort(key=lambda p: p["node"])
for i, p in enumerate(parts):
    print("P%dSTART=%d" % (i, p["start"]))
    print("P%dSECT=%d"  % (i, p["size"]))
')"

dd if="${IMG}" of="${WORK}/p2.img" bs=512 skip="${P1START}" count="${P1SECT}" status=none

for pair in "$@"; do
    src=${pair%%:*}
    dst=${pair#*:}
    [ -f "${src}" ] || { echo "deploy: no such file ${src}" >&2; exit 1; }

    debugfs -w -R "rm ${dst}" "${WORK}/p2.img" >/dev/null 2>&1 || true
    debugfs -w -R "write ${src} ${dst}" "${WORK}/p2.img" >/dev/null 2>&1 ||
        { echo "deploy: failed to write ${dst}" >&2; exit 1; }
    # debugfs creates files 0644; userspace binaries have to stay executable.
    debugfs -w -R "sif ${dst} mode 0100755" "${WORK}/p2.img" >/dev/null 2>&1
    debugfs -w -R "sif ${dst} uid 0" "${WORK}/p2.img" >/dev/null 2>&1
    debugfs -w -R "sif ${dst} gid 0" "${WORK}/p2.img" >/dev/null 2>&1
    echo "deploy: ${src} -> ${dst}"
done

# debugfs bypasses the journal, so check the result before letting the kernel
# see it.
e2fsck -fy "${WORK}/p2.img" >/dev/null 2>&1 || true

dd if="${WORK}/p2.img" of="${IMG}" bs=512 seek="${P1START}" conv=notrunc status=none
echo "deploy: partition 2 (LBA ${P1START}) of ${IMG} updated"
