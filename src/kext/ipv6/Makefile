COMPILER = clang
TOPDIR ?= ${.CURDIR}/../../..
SYSROOT = ${TOPDIR}/target
TRIPLE = x86_64-unknown-elf
MODULE = ipv6.ko
OUTPUT = module.ko
TARGET = ${SYSROOT}/boot/

CFLAGS = -std=c17 -Wall -Wextra \
         -nostdlib -fno-builtin -ffreestanding \
         -fno-pie -fno-pic \
         -mcmodel=kernel -mno-red-zone -fno-stack-protector \
         --target=${TRIPLE} \
         --sysroot=${SYSROOT} \
         -isystem ${SYSROOT}/usr/include \
         -I${TOPDIR}/src/kernel/include \
         -c

.PHONY: all compile install clean
.NOTPARALLEL:

all: clean compile install

compile:
	${COMPILER} main.c -o ${OUTPUT} ${CFLAGS}

install:
	mkdir -p ${TARGET}
	sudo cp ${OUTPUT} ${TARGET}${MODULE}
	sudo chown root:root ${TARGET}${MODULE}

clean:
	rm -f ${OUTPUT}