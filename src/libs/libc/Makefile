COMPILER ?= clang
TARGET_TRIPLE = x86_64-apple-darwin

# Anchor to TOPDIR (exported by the root makefile), with a fallback for
# running bmake directly from this directory.
TOPDIR ?= ${.CURDIR}/../..
SYSROOT = ${TOPDIR}/target
# /lib, not /usr/lib, and the two must agree exactly.
#
# INSTALL_NAME below is baked into LC_ID_DYLIB and into every executable's
# LC_LOAD_DYLIB, and the loader resolves it against the mounted root. If the
# file does not land at precisely that path the loader gets ENOENT at exec time
# with nothing else to go on. /lib is also where this system already keeps its C
# runtime - libc.musl-x86_64.so.1 and ld-musl-x86_64.so.1 both live there -
# so libc.dylib belongs beside them, not in /usr/lib where the bulk of the
# package tree lives.
LIBDIR = ${SYSROOT}/lib
INCLUDEDIR = ${SYSROOT}/usr/include

# Where an executable asks for this library. See LIBDIR above.
INSTALL_NAME = /lib/libc.dylib

LIB_OUTPUT = libc.dylib
HEADER = libc.h
OBJECT = libc.o
EXPORTS = libc.exports

# -fPIC because this is a real dylib: the loader is free to slide it to any
# base, so every internal reference has to survive an arbitrary load address.
CFLAGS = --target=${TARGET_TRIPLE} -std=gnu17 -ffreestanding -fno-builtin \
         -fno-stack-protector -fPIC -fno-omit-frame-pointer -fno-lto \
         -nostdlib -Wall -Wextra -Werror -c

# -nostdlib has to be repeated here. Without it clang links the darwin driver,
# which pulls in libSystem/libgcc_s/dylib1 from the host - none of which exist
# for a cavOS target, and all of which would defeat the point of a freestanding
# dylib.
LDFLAGS = --target=${TARGET_TRIPLE} -fuse-ld=lld -nostdlib -dynamiclib \
          -Wl,-install_name,${INSTALL_NAME} -Wl,-arch,x86_64 \
          -Wl,-platform_version,macos,10.4,10.4

.PHONY: all compile install clean exports

all: compile install

compile: ${LIB_OUTPUT}

${OBJECT}: libc.c ${HEADER}
	${COMPILER} ${CFLAGS} -I. -o ${.TARGET} ${.IMPSRC}

# The export list is derived from the object's own global definitions instead
# of from a hand-maintained list or a scrape of the header. That matters twice
# over: a hand-written list silently rots the moment a function is added, and
# because nothing references libc.dylib when it is linked, lld dead-strips
# every symbol unless it is named explicitly - so a missing entry is a symbol
# that is simply absent at runtime rather than a link error.
#
# nm prints the Mach-O spelling, with its leading underscore already attached,
# and -exported_symbols_list wants exactly that spelling.
exports: ${EXPORTS}

${EXPORTS}: ${OBJECT}
	llvm-nm --defined-only -g ${.ALLSRC} | \
	    awk '$$2 ~ /^[TDBRSC]$$/ { print $$3 }' | sort -u > ${.TARGET}

${LIB_OUTPUT}: ${OBJECT} ${EXPORTS}
	${COMPILER} ${LDFLAGS} -Wl,-exported_symbols_list,${EXPORTS} ${OBJECT} \
	    -o ${.TARGET}

install: compile
	sudo mkdir -p ${LIBDIR} ${INCLUDEDIR}
	sudo cp ${LIB_OUTPUT} ${LIBDIR}/
	sudo cp ${HEADER} ${INCLUDEDIR}/

clean:
	rm -f ${LIB_OUTPUT} ${OBJECT} ${EXPORTS}