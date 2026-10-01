COMPILER = clang
TOPDIR ?= ${.CURDIR}/../../..
SYSROOT = ${TOPDIR}/target
CFLAGS = -std=c17 -pedantic-errors -Wall -Wextra -static --sysroot=${SYSROOT}
OUTPUT = asroot
DESTDIR = ${SYSROOT}/usr/bin

.PHONY: all compile install clean
.NOTPARALLEL:

all: clean compile install

compile:
	${COMPILER} ${CFLAGS} -o ${OUTPUT} main.c

install:
	mkdir -p ${DESTDIR}
	sudo cp ${OUTPUT} ${DESTDIR}/
	sudo chown root:root ${DESTDIR}/${OUTPUT}
	sudo chmod 4755 ${DESTDIR}/${OUTPUT}

clean:
	rm -f ${OUTPUT}