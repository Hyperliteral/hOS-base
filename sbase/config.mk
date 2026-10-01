# sbase version
VERSION = 0.0

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# tools
CC = clang
AR = llvm-ar
RANLIB = ranlib

# Cavos Target paths & flags
SYSROOT   = ../target
CRT1          = $(SYSROOT)/lib/crt1.o
LIBSYSTEM = $(SYSROOT)/lib/libsystem.a
LIBC          = $(SYSROOT)/usr/lib/libc.a

CFLAGS   += --target=x86_64-cavos --sysroot=$(SYSROOT) -nostdlib -nostartfiles
LDFLAGS  += --target=x86_64-cavos --sysroot=$(SYSROOT)