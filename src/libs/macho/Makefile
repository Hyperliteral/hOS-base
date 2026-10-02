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
RUNTIME_OBJS = crt0.o runtime.o

CFLAGS = --target=${TARGET_TRIPLE} -std=gnu17 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-omit-frame-pointer -fno-lto -nostdlib -Wall -Wextra -Werror -c

.PHONY: all compile install clean

all: compile install

compile: ${RUNTIME_OBJS} ${LIB_OUTPUT}

crt0.o: crt0.c macho.h
	${COMPILER} ${CFLAGS} -I. -o ${.TARGET} ${.IMPSRC}

runtime.o: runtime.c macho.h
	${COMPILER} ${CFLAGS} -I. -o ${.TARGET} ${.IMPSRC}

${LIB_OUTPUT}: ${RUNTIME_OBJS}
	${ARCHIVER} rcs ${.TARGET} ${.ALLSRC}

install: compile
	sudo mkdir -p ${LIBDIR} ${INCLUDEDIR}
	sudo cp ${LIB_OUTPUT} ${LIBDIR}/
	sudo cp macho.h ${INCLUDEDIR}/

clean:
	rm -f ${RUNTIME_OBJS} ${LIB_OUTPUT}