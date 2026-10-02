COMPILER = clang
TOPDIR ?= ${.CURDIR}/../../..
SYSROOT = ${TOPDIR}/target
LIBDIR = ${SYSROOT}/lib
USRLIBDIR = ${SYSROOT}/usr/lib
USRINCDIR = ${SYSROOT}/usr/include
TRIPLE = x86_64-unknown-elf
OUTPUT = initd
TARGET = ${SYSROOT}/usr/bin/

CFLAGS = --target=${TRIPLE} -std=c17 -pedantic-errors -Wall -Wextra \
         -nostdlib -nostartfiles \
         -isystem ${USRINCDIR}

# cavOS binaries link against our own crt1.o (src/libs/system) and the
# libsystem.a syscall wrappers, with musl providing libc. The host toolchain's
# default startup files and compiler-rt are not part of the target, so the crt
# objects are named explicitly and -nostdlib/-nostartfiles disable clang's
# built-in selection.
#
# These must stay *after* the sources on the link line: ld only pulls archive
# members in response to undefined references, so archives listed before the
# object that needs them are silently ignored.
LIBS = ${LIBDIR}/crt1.o \
       -L${LIBDIR} -L${USRLIBDIR} -lsystem -lc \
       ${USRLIBDIR}/crti.o ${USRLIBDIR}/crtn.o

.PHONY: all compile install clean
.NOTPARALLEL:

all: clean compile install

compile:
	${COMPILER} main.c -o ${OUTPUT} ${CFLAGS} ${LIBS}

install:
	mkdir -p ${TARGET}
	sudo cp ${OUTPUT} ${TARGET}

clean:
	rm -f ${OUTPUT}