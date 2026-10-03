#!/bin/sh
# Deploy-only helper: push an already-built target/boot/kernel.elf into the
# existing disk.img *without* needing sudo or a loop mount.
#
# The Limine config used by cavOS loads the kernel from the FAT boot partition
# (kernel_path: boot():/kernel.elf), so only partition 1 needs touching. That
# partition is edited with mtools and spliced back into the image with dd,
# neither of which require privileges.
#
# Usage: tools/kernel/deploy_kernel.sh [image]
set -e

SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
TOP=$(realpath "${SCRIPTPATH}/../..")
IMG=${1:-${TOP}/disk.img}
WORK=$(mktemp -d)
trap 'rm -rf "${WORK}"' EXIT

KERNEL="${TOP}/target/boot/kernel.elf"
if [ ! -f "${KERNEL}" ]; then
    echo "deploy: no kernel at ${KERNEL}; run 'bmake kernel' first" >&2
    exit 1
fi

# Partition 1 geometry, read straight from the image rather than hardcoded.
eval "$(sfdisk -l -o Start,Sectors --json "${IMG}" |
    python3 -c '
import json,sys
p=json.load(sys.stdin)["partitiontable"]["partitions"]
p.sort(key=lambda x: x["node"])
print("PSTART=%d" % p[0]["start"])
print("PSECT=%d"  % p[0]["size"])
')"

dd if="${IMG}" of="${WORK}/p1.img" bs=512 skip="${PSTART}" count="${PSECT}" status=none

MTOOLS_SKIP_CHECK=1 mcopy -o -i "${WORK}/p1.img" "${KERNEL}" ::/kernel.elf

dd if="${WORK}/p1.img" of="${IMG}" bs=512 seek="${PSTART}" conv=notrunc status=none

echo "deploy: kernel.elf ($(wc -c <"${KERNEL}") bytes) written to ${IMG} partition 1 (LBA ${PSTART})"
