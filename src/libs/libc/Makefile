COMPILER ?= clang

TARGET_TRIPLE_MHO = x86_64-apple-darwin
TARGET_TRIPLE_ELF = x86_64-unknown-elf

# Anchor to TOPDIR
TOPDIR ?= ${.CURDIR}/../..
SYSROOT = ${TOPDIR}/target
LIBDIR = ${SYSROOT}/lib
INCLUDEDIR = ${SYSROOT}/usr/include

# Build directory for object files
OBJDIR = obj

# Dynamically find all C sources and Headers in the current dir and subdirectories
SRCS != find . -type f -name '*.c'
HDRS != find . -type f -name '*.h'

# Include paths: current directory and the 'include' directory relative to this Makefile
INCLUDE_DIRS = -I. -I${.CURDIR}/include

# --- Mach-O Configuration ---
LIB_OUTPUT_MHO = libc.dylib
# Prepend OBJDIR/ to every source file and change .c to .mho.o
OBJS_MHO = ${SRCS:S/^/${OBJDIR}\//:.c=.mho.o}
EXPORTS_MHO = libc_mho.exports
INSTALL_NAME_MHO = /lib/${LIB_OUTPUT_MHO}

# --- ELF Configuration ---
LIB_OUTPUT_ELF = libc.so
# Prepend OBJDIR/ to every source file and change .c to .elf.o
OBJS_ELF = ${SRCS:S/^/${OBJDIR}\//:.c=.elf.o}
EXPORTS_ELF = libc_elf.exports
INSTALL_NAME_ELF = /lib/${LIB_OUTPUT_ELF}

# Common CFLAGS for both architectures
BASE_CFLAGS = -std=c17 -ffreestanding -fno-builtin \
          -fno-stack-protector -fPIC -fno-omit-frame-pointer -fno-lto \
          -nostdlib -Wall -Wextra -Werror -c

# Specific Compiler & Linker Flags
CFLAGS_MHO = --target=${TARGET_TRIPLE_MHO} ${BASE_CFLAGS}
LDFLAGS_MHO = --target=${TARGET_TRIPLE_MHO} -fuse-ld=lld -nostdlib -dynamiclib \
          -Wl,-install_name,${INSTALL_NAME_MHO} -Wl,-arch,x86_64 \
          -Wl,-platform_version,macos,10.4,10.4 \
          -Wl,-exported_symbols_list,${EXPORTS_MHO}

CFLAGS_ELF = --target=${TARGET_TRIPLE_ELF} ${BASE_CFLAGS}
LDFLAGS_ELF = --target=${TARGET_TRIPLE_ELF} -fuse-ld=lld -nostdlib -shared \
          -Wl,-soname,${INSTALL_NAME_ELF}


.PHONY: all mho elf install clean

# Default target does both
all: mho elf

mho: ${LIB_OUTPUT_MHO}

elf: ${LIB_OUTPUT_ELF}


# ==========================================
# Object Compilation Rules
# ==========================================
# Loop over every discovered source file and create individual compilation targets
.for src in ${SRCS}
${OBJDIR}/${src:.c=.mho.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_MHO} ${INCLUDE_DIRS} -o ${.TARGET} ${src}

${OBJDIR}/${src:.c=.elf.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_ELF} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
.endfor


# ==========================================
# Mach-O Build Rules
# ==========================================
${EXPORTS_MHO}: ${OBJS_MHO}
	llvm-nm --defined-only -g ${.ALLSRC} | \
	    awk '$$2 ~ /^[TDBRSC]$$/ { print $$3 }' | sort -u > ${.TARGET}

${LIB_OUTPUT_MHO}: ${OBJS_MHO} ${EXPORTS_MHO}
	${COMPILER} ${LDFLAGS_MHO} ${OBJS_MHO} -o ${.TARGET}


# ==========================================
# ELF Build Rules
# ==========================================
${EXPORTS_ELF}: ${OBJS_ELF}
	llvm-nm --defined-only -g ${.ALLSRC} | \
	    awk '$$2 ~ /^[TDBRSC]$$/ { print $$3 }' | sort -u > ${.TARGET}

${LIB_OUTPUT_ELF}: ${OBJS_ELF} ${EXPORTS_ELF}
	${COMPILER} ${LDFLAGS_ELF} ${OBJS_ELF} -o ${.TARGET}


# ==========================================
# Install & Clean
# ==========================================
install: all
	sudo mkdir -p ${LIBDIR} ${INCLUDEDIR}
	sudo cp ${LIB_OUTPUT_MHO} ${LIB_OUTPUT_ELF} ${LIBDIR}/
	# Copy all discovered headers. Note: this flattens them into INCLUDEDIR.
	sudo cp ${HDRS} ${INCLUDEDIR}/

clean:
	rm -f ${LIB_OUTPUT_MHO} ${EXPORTS_MHO}
	rm -f ${LIB_OUTPUT_ELF} ${EXPORTS_ELF}
	rm -rf ${OBJDIR}