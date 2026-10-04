# Anchor absolute paths for bmake compatibility with GNU Make sub-makefiles
CURDIR ?= ${.CURDIR}
TOPDIR ?= ${.CURDIR}
.export CURDIR TOPDIR

all: disk

.PHONY: disk tools clean qemu qemu_dbg vmware dev kernel musl remusl cleanmusl limine ports macho libc system config

config:
	python3 tools/config/configure.py

# Musl libc
remusl: cleanmusl musl

SOFTWARE_DIRS != find src/software -mindepth 2 -maxdepth 2 -name Makefile 2>/dev/null || true
SOFTWARE_DIRS := ${SOFTWARE_DIRS:H}

cleanmusl:
	rm -rf src/libs/musl/cavos-build
	rm -rf src/libs/musl/cavos-out/include
	rm -rf src/libs/musl/cavos-out/lib

musl:
	chmod +x src/libs/musl/build.sh
	./src/libs/musl/build.sh --noreplace

macho:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/libs/macho

# libc.dylib, the Mach-O C library. Kept separate from the "macho" target above
# because it is a different kind of artifact: that one is a static archive
# holding only the entry stub, this one is a real dylib that executables link
# against by name and resolve at load time.
libc-m:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/libs/libc mho

libc-e:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/libs/libc elf

# Userspace startup code and syscall wrappers. Every binary in SOFTWARE_DIRS
# links against ${LIBDIR}/crt1.o and ${LIBDIR}/libsystem.a, so this has to run
# before them - otherwise they silently relink against a stale crt1.o left
# behind by an earlier build.
system:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/libs/system

# Essential ports
ports:
	chmod +x tools/userland/ports.sh
	CURDIR=${CURDIR} TOPDIR=${TOPDIR} tools/userland/ports.sh

# Limine
relimine: cleanlimine limine
    
cleanlimine:
	rm -rf src/bootloader/limine
	rm -f src/bootloader/.version

limine:
	chmod +x src/bootloader/assert.sh
	src/bootloader/assert.sh

uacpi:
	chmod +x tools/kernel/uacpi.sh
	tools/kernel/uacpi.sh

# Primary (disk creation)
# `libc` has to precede the software loop below, because every Mach-O binary in
# it links against libc.dylib by name and needs the file to exist at link time.
disk_prepare: limine uacpi musl ports macho libc system
# `|| exit 1` is load-bearing: without it the loop's exit status is the status of
# the last iteration only, so a userspace build that failed early would still let
# make go on and bake a broken image.
	@for dir in ${SOFTWARE_DIRS}; do \
		${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C "$$dir" -j1 || exit 1; \
	done

disk: disk_prepare clean
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel disk

disk_dirty: disk_prepare
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel disk_dirty

# Raw kernel.elf
kernel:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel all

# Tools
tools:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel tools

clean:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel clean

# Hypervisors
qemu:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel qemu

qemu_dbg:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel qemu_dbg

vmware:
	@${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C src/kernel vmware

dev: clean disk_dirty qemu_dbg

# Code formatting tools
FIND_CMD = find src/kernel/ -type f \
	\( -name '*.c' -o -name '*.h' \) \
	-not -path 'src/kernel/core/acpi/uacpi/*' \
	-not -path 'src/kernel/include/uacpi/*' \
	-not -path 'src/kernel/services/net/lwip/*' \
	-not -name 'printf.c' \
	-not -name 'printf.h' \
	-not -name 'malloc.c' \
	-not -name 'malloc.h' \
	-not -name 'limine.h'

CLANG_FORMAT_STYLE := -style='{ BasedOnStyle: LLVM, AlignConsecutiveDeclarations: true, IndentWidth: 2 }'
CLANG_FORMAT_TARGET = "$(HOME)/opt/clang-format-cavos"

format_prerequisites:
	@chmod +x tools/toolchain/get_formatter.sh
	@tools/toolchain/get_formatter.sh

format: format_prerequisites
	@${FIND_CMD} | xargs ${CLANG_FORMAT_TARGET} ${CLANG_FORMAT_STYLE} -i

format_check: format_prerequisites
	@${FIND_CMD} | xargs ${CLANG_FORMAT_TARGET} ${CLANG_FORMAT_STYLE} --dry-run --Werror

.NOTPARALLEL: