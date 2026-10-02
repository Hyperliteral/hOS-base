COMPILER = clang
ARCHIVER = llvm-ar
TARGET_TRIPLE = x86_64-cavos
SYSROOT = $(.CURDIR)/../../../target
TRIPLE = x86_64-unknown-elf
LIBDIR = $(SYSROOT)/lib

CFLAGS = -std=c17 -pedantic-errors -ffreestanding -Wall -Wextra -fno-builtin -nostartfiles -nostdlib -c -fpic --target=$(TARGET_TRIPLE)

SYSTEM_OBJS = main.o
CRT_OBJ = crt0.o
LIB_OUTPUT = libsystem.a
CRT_OUTPUT = crt1.o

all: clean compile install

compile:
	$(COMPILER) $(CFLAGS) main.c -o main.o
	$(COMPILER) $(CFLAGS) crt0.c -o crt0.o
	$(ARCHIVER) rcs $(LIB_OUTPUT) $(SYSTEM_OBJS)
	sudo cp crt0.o $(CRT_OUTPUT)

install:
	mkdir -p $(LIBDIR)
	sudo cp $(LIB_OUTPUT) $(LIBDIR)/
	sudo cp $(CRT_OUTPUT) $(LIBDIR)/

clean:
	rm -f $(SYSTEM_OBJS) $(CRT_OBJ) $(LIB_OUTPUT) $(CRT_OUTPUT)