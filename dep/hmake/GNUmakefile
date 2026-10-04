# hmake - the cavOS fork of bmake.
#
# Makefile-only build: no ./configure, no boot-strap, no generated files.
# Everything the build needs is defined right here; the feature answers that
# configure used to detect live in the checked-in config.h (Linux/glibc).
#
# GNU make picks this file automatically (GNUmakefile wins over Makefile),
# so a plain `make -C dep/hmake` from the repo root just works:
#
#   make -C dep/hmake          # builds ./hmake
#   sudo make -C dep/hmake install   # installs binary + mk macros + man page
#
# All settings are ?= so the environment (or `make CC=clang`) can override.

PROG     = hmake
CC      ?= cc
CFLAGS  ?= -g -O2
LDFLAGS ?=
PREFIX  ?= /usr/local

# Feature answers for this build. Linux/glibc provides everything except
# stresep(3), hence the two compat objects at the end of OBJS.
CPPFLAGS = -I. -DMAKE_NATIVE -DHAVE_CONFIG_H -DUSE_META

# Platform identity, baked in the same way boot-strap used to do it.
# Upstream mapped these through machine.sh to NetBSD names; plain uname
# output is fine for a Linux build tool and can be overridden per platform.
MACHINE      ?= $(shell uname -m)
MACHINE_ARCH ?= $(MACHINE)
MAKE_OS      ?= $(shell uname -s)

MAKE_VERSION := $(shell sed -n 's/^_MAKE_VERSION=//p' VERSION)

DEFSYSPATH = $(PREFIX)/share/mk

MAIN_DEFS = \
	-DMAKE_VERSION=\"$(MAKE_VERSION)\" \
	-DMACHINE=\"$(MACHINE)\" \
	-DMACHINE_ARCH=\"$(MACHINE_ARCH)\" \
	-DMAKE_OS=\"$(MAKE_OS)\" \
	-D_PATH_DEFSYSPATH=\"$(DEFSYSPATH)\" \
	-DMAKE_SAVE_DOLLARS_DEFAULT=\"no\" \
	-DBMAKE_PATH_MAX=1024

OBJS = \
	arch.o \
	buf.o \
	compat.o \
	cond.o \
	dir.o \
	for.o \
	hash.o \
	job.o \
	lst.o \
	main.o \
	make.o \
	make_malloc.o \
	meta.o \
	metachar.o \
	parse.o \
	sigcompat.o \
	stresep.o \
	str.o \
	suff.o \
	targ.o \
	trace.o \
	util.o \
	var.o

.PHONY: all clean install test

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

main.o: main.c VERSION
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MAIN_DEFS) -c -o $@ main.c

job.o: job.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MAIN_DEFS) -c -o $@ job.c

meta.o: meta.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MAIN_DEFS) -c -o $@ meta.c

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) $(OBJS)

# Upstream's regression suite. unit-tests/Makefile.config used to be stamped
# out by configure from Makefile.config.in; the same substitution happens
# here so the suite runs Makefile-only. Four tests (cmdline, meta-output,
# objdir-writable, varname-empty) fail in containers as root - the pristine
# upstream tree fails them identically, so that is environmental, not a fork
# regression.
test: $(PROG)
	sed -e 's/@diff_u@/-u/' \
	    -e 's/@egrep@/grep -E/' \
	    -e 's/@lua@//' \
	    -e 's/@diff@/diff/' \
	    -e 's/@UTC_1@/Europe\/Berlin/' \
	    -e 's|@srcdir@|$(CURDIR)/unit-tests|' \
	    unit-tests/Makefile.config.in > unit-tests/Makefile.config
	cd unit-tests && MAKEFLAGS= ./../$(PROG) -r -m / test $(if $(TESTS),TESTS="$(TESTS)")

install: $(PROG)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	install -d $(DESTDIR)$(PREFIX)/share/mk
	cp -r mk/. $(DESTDIR)$(PREFIX)/share/mk/
	install -d $(DESTDIR)$(PREFIX)/share/man/man1
	install -m 644 hmake.1 $(DESTDIR)$(PREFIX)/share/man/man1/hmake.1
