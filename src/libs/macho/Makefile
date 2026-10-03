COMPILER ?= clang
ARCHIVER ?= llvm-ar
TARGET_TRIPLE = x86_64-none-macho

# Anchor to TOPDIR (exported by the root makefile), with a fallback for
# running bmake directly from this directory.
TOPDIR ?= ${.CURDIR}/../../..
SYSROOT = ${TOPDIR}/target
LIBDIR = ${SYSROOT}/usr/lib
INCLUDEDIR = ${SYSROOT}/usr/include
LIB_OUTPUT = libmacho.a

# This is the Mach-O *program startup* half of the old combined library: the
# entry stub that turns the kernel-provided stack pointer into argc/argv/envp
# and calls main. The libc half - allocator, string, stdio, syscall stubs -
# moved to src/libs/libc and is now built as a real dylib, libc.dylib, so that
# dynamic linking has something to actually resolve against.
#
# crt0 is still linked into each executable directly. A dylib cannot supply the
# entry point: the kernel jumps to LC_MAIN's offset in the executable itself.
RUNTIME_OBJS = crt0.o

CFLAGS = --target=${TARGET_TRIPLE} -std=gnu17 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-omit-frame-pointer -fno-lto -nostdlib -Wall -Wextra -Werror -c

# The libc declarations crt0 needs. They resolve against libc.dylib at load
# time, so crt0 deliberately does not link any of them itself.
INCLUDES = -I${TOPDIR}/src/libs/libc

.PHONY: all compile install clean

all: compile install

compile: ${RUNTIME_OBJS} ${LIB_OUTPUT}

crt0.o: crt0.c
	${COMPILER} ${CFLAGS} ${INCLUDES} -o ${.TARGET} ${.IMPSRC}

${LIB_OUTPUT}: ${RUNTIME_OBJS}
	${ARCHIVER} rcs ${.TARGET} ${.ALLSRC}

install: compile
	sudo mkdir -p ${LIBDIR} ${INCLUDEDIR}
	sudo cp ${LIB_OUTPUT} ${LIBDIR}/

clean:
	rm -f ${RUNTIME_OBJS} ${LIB_OUTPUT}