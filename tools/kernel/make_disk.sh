#!/usr/bin/env bash
set -x # show cmds
set -e # fail globally

# $1 -> target system, $2 -> mounted volume, $3 -> image file
if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
        echo "Please supply the correct arguments!"
        exit 1
fi
# NOTE: $4 -> quick kernel copy

SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

if [ ! -d "$1/bin" ]; then
        cd "$1"
        ln -s usr/bin bin
        cd "$SCRIPTPATH"
fi

if test -f /dev/loop101; then
        echo "Loopback device designated for cavOS is already assigned to something! (/dev/loop101)"
        exit 1
fi

LIMINE_DIR="${SCRIPTPATH}/../../src/bootloader/limine/"
LIMINE_EXEC="${LIMINE_DIR}/limine"
LIMINE_CONF="${SCRIPTPATH}/../../src/bootloader/limine.conf"

if [ ! -f "${LIMINE_EXEC}" ]; then
        echo "Please compile the bootloader (limine) first!"
        exit 1
fi

# Stage the UEFI loader into the target tree before anything is imaged. The
# "${1}/*" copy further down then carries it onto the boot partition alongside
# the BIOS loader and the kernel, and the image sizing below accounts for it.
mkdir -p "${1}/boot/limine"
cp "$LIMINE_DIR/BOOTX64.EFI" "${1}/boot/limine/"

SIZE_IN_BYTES=$(du -sb "$1" | cut -f1)
SIZE_IN_BLOCKS=$((($SIZE_IN_BYTES / 512) * 2 + 500000))

if [ -z "$4" ]; then
        dd if=/dev/zero of="${3}" bs=512 count=$SIZE_IN_BLOCKS
        parted "${3}" mklabel msdos
        parted "${3}" mkpart primary ext4 2048s 68157440B
        parted "${3}" set 1 boot on
        parted "${3}" mkpart primary ext4 136314880B 100%
        "$LIMINE_EXEC" bios-install "${3}"
fi

sudo losetup -P /dev/loop101 "${3}"

if [ -z "$4" ]; then
        sudo mkdosfs -F32 -f 2 /dev/loop101p1 || sudo mkfs.fat -F32 -f 2 /dev/loop101p1
        sudo mke2fs -L "cavOS" -O ^dir_index /dev/loop101p2 "$(((($SIZE_IN_BLOCKS - 350000) * 512) / 1024))"
        sudo fatlabel /dev/loop101p1 LIMINE
fi

sudo mkdir -p "${2}"
sudo mount /dev/loop101p2 "${2}"
sudo mkdir -p "${2}/boot/"
sudo mount /dev/loop101p1 "${2}/boot/"

sudo mkdir -p "${2}/boot/limine/" "${2}/boot/EFI/BOOT"
sudo cp "$LIMINE_DIR/limine-bios.sys" "${2}/boot/limine/"
sudo cp "$LIMINE_DIR/BOOTX64.EFI" "${2}/boot/EFI/BOOT"
sudo cp "$LIMINE_DIR/BOOTIA32.EFI" "${2}/boot/EFI/BOOT"

# NOTE: "${2}/boot" is the mount point of the *FAT boot partition*, so paths
# below it are relative to the root of that partition, not to the ext4 root.
# "/limine.conf" is the first candidate Limine looks for, and works for both
# BIOS and UEFI.
if [ -f "$LIMINE_CONF" ]; then
        sudo cp "$LIMINE_CONF" "${2}/boot/limine.conf"
else
        echo "NOTE: no Limine config at ${LIMINE_CONF};" \
                "installing no limine.conf onto the boot partition." >&2
fi

# UPDATED LINE: Added --no-preserve=ownership to prevent FAT32 errors
if [ -z "$4" ]; then
        sudo cp -a --no-preserve=ownership ${1}/* "${2}/"
else
        sudo cp -a --no-preserve=ownership ${1}/boot/* "${2}/boot/"
fi

sudo rm -f "${2}/.gitignore"

CURRENT_DIRECTORY=$(dirname "$0")
chmod +x "${CURRENT_DIRECTORY}/cleanup.sh"
"${CURRENT_DIRECTORY}/cleanup.sh" "${2}"