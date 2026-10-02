# Anchor absolute paths for bmake compatibility with GNU Make sub-makefiles
CURDIR ?= ${.CURDIR}
TOPDIR ?= ${.CURDIR}
.export CURDIR TOPDIR

all: disk

.PHONY: disk tools clean qemu qemu_dbg vmware dev kernel musl remusl cleanmusl limine ports macho config

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
disk_prepare: limine uacpi musl ports macho
	@for dir in ${SOFTWARE_DIRS}; do \
		${.MAKE} CURDIR=${CURDIR} TOPDIR=${TOPDIR} -C "$$dir" -j1; \
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