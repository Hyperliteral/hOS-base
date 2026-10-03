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

# Only target/ is root-owned (ports populates it), so this is the sole step that
# needs sudo. crt0.o is installed under the crt1.o name the link lines expect,
# so there is no need to stage a second copy in the build directory.
install:
	mkdir -p $(LIBDIR)
	sudo cp $(LIB_OUTPUT) $(LIBDIR)/
	sudo cp $(CRT_OBJ) $(LIBDIR)/$(CRT_OUTPUT)

clean:
	rm -f $(SYSTEM_OBJS) $(CRT_OBJ) $(LIB_OUTPUT) $(CRT_OUTPUT)