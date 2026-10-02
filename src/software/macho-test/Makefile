COMPILER = ${TOPDIR}/tools/userland/macho-clang
RUNTIME = ${TOPDIR}/src/libs/macho
OUTPUT = macho-test
TARGET = ${TOPDIR}/target/usr/bin/
CFLAGS = -Wall -Wextra -Werror

# Anchor to TOPDIR (exported by the root makefile), with a fallback for
# running bmake directly from this directory.
TOPDIR ?= ${.CURDIR}/../../..

.PHONY: all runtime compile install clean
.NOTPARALLEL:

all: clean install

# Only the library is needed here: tools/userland/macho-clang links the
# source-tree libmacho.a directly. Installing into the sysroot is the job of
# the top-level "macho" target, so skip it to avoid a redundant root-owned
# copy on every build.
runtime:
	${.MAKE} CURDIR=${.CURDIR} TOPDIR=${TOPDIR} -C ${RUNTIME} compile

compile: runtime
	${COMPILER} ${CFLAGS} main.c -o ${OUTPUT}

install: compile
	mkdir -p ${TARGET}
	sudo cp ${OUTPUT} ${TARGET}

clean:
	rm -f ${OUTPUT}