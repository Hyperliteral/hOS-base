COMPILER = clang
ASSEMBLER = nasm
AR = llvm-ar

TOPDIR ?= ${.CURDIR}/../../..

CFLAGS = --target=x86_64-elf -m64 -g -c -ffreestanding -Wall -Werror -fcommon \
    -I../include/ \
    -I../services/net/lwip/include/ \
    -isystem ${TOPDIR}/target/usr/include \
    -fno-pie -fno-pic \
    -mcmodel=kernel \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -nostdlib \
    -mno-sse2 \
    -mno-red-zone -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-omit-frame-pointer \
    -include config.h

ASFLAGS = -f elf64

C_SOURCES != find . -name '*.c' ! -name malloc.c ! -name printf.c | sed 's|^\./||' | sort
ASM_SOURCES != find . -name '*.asm' | sed 's|^\./||' | sort

C_OBJS = ${C_SOURCES:.c=.o}
ASM_OBJS = ${ASM_SOURCES:=.o}
C_EXTRA_OBJS = memory/malloc.o debug/printf.o

TARGET = core.a

.PHONY: all clean

all: ${TARGET}

${TARGET}: ${C_OBJS} ${ASM_OBJS} ${C_EXTRA_OBJS}
	${AR} rcs ${.TARGET} ${.ALLSRC}

memory/malloc.o: memory/malloc.c
	${COMPILER} ${CFLAGS} memory/malloc.c -o memory/malloc.o -DHAVE_MMAP=0 -DLACKS_TIME_H=1 -DLACKS_SYS_PARAM_H=1 -DLACKS_STRING_H=0 -Dmalloc_getpagesize=4096 -DNO_MALLOC_STATS=1 -DMORECORE_CONTIGUOUS=0 -DUSE_LOCKS=2 "-DMALLOC_FAILURE_ACTION=abort()"

debug/printf.o: debug/printf.c
	${COMPILER} ${CFLAGS} debug/printf.c -o debug/printf.o -DPRINTF_INCLUDE_CONFIG_H=1

.for src in ${C_SOURCES}
${src:R}.o: ${src}
	${COMPILER} ${CFLAGS} ${src} -o ${.TARGET}
.endfor

.for src in ${ASM_SOURCES}
${src}.o: ${src}
	${ASSEMBLER} ${ASFLAGS} -o ${.TARGET} ${src}
.endfor

clean:
	find . -name '*.o' -delete
	rm -f ${TARGET}