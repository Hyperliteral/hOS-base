COMPILER = clang
ASSEMBLER = nasm
AR = llvm-ar

# Updated target to standard 64-bit ELF
CFLAGS = --target=x86_64-elf -m64 -g -c -ffreestanding -Wall -Werror -fcommon \
    -I../include/ \
    -I../services/net/lwip/include/ \
    -isystem ../../../target/usr/include \
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

# Updated output format for NASM assembly files to ELF64
ASFLAGS = -f elf64

C_SOURCES := $(shell find . -name '*.c' ! -name "malloc.c" ! -name "printf.c" -printf "%P\n")
ASM_SOURCES := $(shell find . -name '*.asm' -printf "%P\n")

C_OBJS = $(patsubst %.c,%.o,$(C_SOURCES))
ASM_OBJS = $(patsubst %.asm,%.asm.o,$(ASM_SOURCES))
C_EXTRA_OBJS = memory/malloc.o debug/printf.o

TARGET = core.a

all: $(TARGET)

$(TARGET): $(C_OBJS) $(ASM_OBJS) $(C_EXTRA_OBJS)
	$(AR) rcs $@ $^

memory/malloc.o: memory/malloc.c
	$(COMPILER) $(CFLAGS) memory/malloc.c -o memory/malloc.o -DHAVE_MMAP=0 -DLACKS_TIME_H=1 -DLACKS_SYS_PARAM_H=1 -DLACKS_STRING_H=0 -Dmalloc_getpagesize=4096 -DNO_MALLOC_STATS=1 -DMORECORE_CONTIGUOUS=0 -DUSE_LOCKS=2 "-DMALLOC_FAILURE_ACTION=abort()"

debug/printf.o: debug/printf.c
	$(COMPILER) $(CFLAGS) debug/printf.c -o debug/printf.o -DPRINTF_INCLUDE_CONFIG_H=1

%.o: %.c
	$(COMPILER) $(CFLAGS) $(subst .o,.c,$@) -o $@

%.asm.o: %.asm
	$(ASSEMBLER) $(ASFLAGS) -o $@ $(subst .asm.o,.asm,$@)

clean:
	find . -name '*.o' -delete
	rm -f $(TARGET)

.PHONY: all clean