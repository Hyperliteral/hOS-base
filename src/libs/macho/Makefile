COMPILER ?= clang
ARCHIVER ?= llvm-ar
TARGET_TRIPLE = x86_64-none-macho
SYSROOT = $(CURDIR)/../../../target
LIBDIR = $(SYSROOT)/usr/lib
INCLUDEDIR = $(SYSROOT)/usr/include
LIB_OUTPUT = libmacho.a
RUNTIME_OBJS = crt0.o runtime.o

CFLAGS = --target=$(TARGET_TRIPLE) -std=gnu17 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-omit-frame-pointer -fno-lto -nostdlib -Wall -Wextra -Werror -c

.PHONY: all compile install clean

all: compile install

compile: $(RUNTIME_OBJS) $(LIB_OUTPUT)

crt0.o: crt0.c macho.h
	$(COMPILER) $(CFLAGS) -I. -o $@ $<

runtime.o: runtime.c macho.h
	$(COMPILER) $(CFLAGS) -I. -o $@ $<

$(LIB_OUTPUT): $(RUNTIME_OBJS)
	$(ARCHIVER) rcs $@ $^

install: compile
	mkdir -p $(LIBDIR) $(INCLUDEDIR)
	cp $(LIB_OUTPUT) $(LIBDIR)/
	cp macho.h $(INCLUDEDIR)/

clean:
	rm -f $(RUNTIME_OBJS) $(LIB_OUTPUT)
