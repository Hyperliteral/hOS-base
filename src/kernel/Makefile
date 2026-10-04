LINKER = ld.lld

LDFLAGS = \
    -m elf_x86_64 \
    -static \
    -e _start \
    --image-base=0xffffffff80000000 \
    --no-undefined

TOPDIR ?= ${.CURDIR}/../..
TARGET = ${TOPDIR}/target
MOUNTPOINT = /cavosmnt
TOOLS = ${TOPDIR}/tools

TARGET_IMG = ${TOPDIR}/disk.img
TARGET_VMWARE = ${TOPDIR}/disk.vmdk
TARGET_ISO = ${TOPDIR}/cavOS.iso

OUTPUT = ${TARGET}/boot/kernel.elf

CORE = core/core.a
SERVICES = services/services.a

CONFIG_MK = ${.CURDIR}/include/config.mk

# Generate defaults if config.mk doesn't exist yet, so it never crashes
.if !exists(${CONFIG_MK})
_CONFIG_GEN != python3 ${TOOLS}/config/configure.py --defaults
.endif

.include "${CONFIG_MK}"
.export CONFIG_CFLAGS

.PHONY: all config disk disk_dirty vmware tools clean qemu qemu_dbg
# The archives are sub-make products, and this makefile has no way to know when
# a .c under core/ or services/ changed. Without .PHONY the archive is simply
# "up to date" as soon as it exists, so edits are silently never compiled.
# Recursing unconditionally is cheap: the sub-makes do the incremental work.
.PHONY: ${CORE} ${SERVICES}

all: ${CORE} ${SERVICES}
	mkdir -p ${TARGET}/boot
	${LINKER} ${LDFLAGS} -o ${OUTPUT} ${CORE} ${SERVICES}

config:
	python3 ${TOOLS}/config/configure.py

${CORE}:
	${.MAKE} -C core

${SERVICES}:
	${.MAKE} -C services

disk: all
	chmod +x ${TOOLS}/kernel/make_disk.sh
	${TOOLS}/kernel/make_disk.sh ${TARGET} ${MOUNTPOINT} ${TARGET_IMG} || (chmod +x ${TOOLS}/kernel/cleanup.sh && ${TOOLS}/kernel/cleanup.sh ${MOUNTPOINT})

disk_dirty: all
	chmod +x ${TOOLS}/kernel/make_disk.sh
	${TOOLS}/kernel/make_disk.sh ${TARGET} ${MOUNTPOINT} ${TARGET_IMG} yes || (chmod +x ${TOOLS}/kernel/cleanup.sh && ${TOOLS}/kernel/cleanup.sh ${MOUNTPOINT})

vmware:
	qemu-img convert ${TARGET_IMG} -O vmdk ${TARGET_VMWARE}

clean:
	${.MAKE} -C core clean
	${.MAKE} -C services clean
	rm -f ${OUTPUT} ${CORE} ${SERVICES}

qemu:
	qemu-system-x86_64 -d guest_errors -serial stdio -drive file=${TARGET_IMG},format=raw,id=disk,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 4g -netdev user,id=mynet0 -net nic,model=e1000,netdev=mynet0 -vga vmware -display sdl -enable-kvm

qemu_dbg:
	qemu-system-x86_64 -d guest_errors -no-shutdown -no-reboot -serial stdio -drive file=${TARGET_IMG},format=raw,id=disk,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 8g -netdev user,id=mynet0,hostfwd=udp::5555-:69,hostfwd=tcp::5555-:69 -net nic,model=e1000,netdev=mynet0 -object filter-dump,id=id,netdev=mynet0,file=${TOPDIR}/netdmp.pcapng -s -S -smp 4 -vga vmware -display sdl -enable-kvm