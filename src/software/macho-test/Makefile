COMPILER = ${TOPDIR}/tools/userland/macho-clang
LIBC = ${TOPDIR}/src/libs/libc
MACHO = ${TOPDIR}/src/libs/macho
OUTPUT = macho-test
TARGET = ${TOPDIR}/target/usr/bin/
CFLAGS = -Wall -Wextra -Werror

# Anchor to TOPDIR (exported by the root makefile), with a fallback for
# running hmake directly from this directory.
TOPDIR ?= ${.CURDIR}/../../..

.PHONY: all runtime compile install clean
.NOTPARALLEL:

all: clean install

# Both halves have to exist before anything can link: the entry stub is pulled
# in directly, and libc.dylib has to be present for lld to record the
# LC_LOAD_DYLIB and emit the import stubs. Installing into the sysroot is the
# job of the top-level "macho" and "libc" targets, so only build here - that
# also avoids a redundant root-owned copy on every build.
runtime:
	@${.MAKE} CURDIR=${.CURDIR} TOPDIR=${TOPDIR} -C ${MACHO} compile
	@${.MAKE} CURDIR=${.CURDIR} TOPDIR=${TOPDIR} -C ${LIBC} compile

compile: runtime
	${COMPILER} ${CFLAGS} main.c -o ${OUTPUT}

install: compile
	mkdir -p ${TARGET}
	sudo cp ${OUTPUT} ${TARGET}

clean:
	rm -f ${OUTPUT}