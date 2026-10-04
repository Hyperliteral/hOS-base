COMPILER = clang
TOPDIR ?= ${.CURDIR}/../../..
SYSROOT = ${TOPDIR}/target
LIBDIR = ${SYSROOT}/lib
USRLIBDIR = ${SYSROOT}/usr/lib
# Our own headers (musl-compatible tree) - this binary tests our libc, so it
# compiles against our include/ rather than the target's musl headers.
OURLIBDIR = ${TOPDIR}/src/libs/libc
OUROWNINCDIR = ${OURLIBDIR}/include
TRIPLE = x86_64-unknown-elf
OUTPUT = thread-test
TARGET = ${SYSROOT}/usr/bin/

CFLAGS = --target=${TRIPLE} -std=gnu17 -D_GNU_SOURCE -Wall -Wextra \
         -ffreestanding -fno-builtin -nostdlib -nostartfiles \
         -isystem ${OUROWNINCDIR}

# Links our static libc.a directly (NOT -lc, which would resolve to musl):
# crt objects named explicitly, archives after the sources on the link line.
LIBS = ${LIBDIR}/crt1.o \
       ${OURLIBDIR}/libc.a \
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
