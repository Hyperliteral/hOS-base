COMPILER ?= clang
NASM ?= nasm
NASMFLAGS_MHO = -f macho64
NASMFLAGS_ELF = -f elf64

TARGET_TRIPLE_MHO = x86_64-apple-darwin
TARGET_TRIPLE_ELF = x86_64-unknown-elf

# Anchor to TOPDIR
TOPDIR ?= ${.CURDIR}/../..
SYSROOT = ../../../target
LIBDIR = ${SYSROOT}/lib
INCLUDEDIR = ${SYSROOT}/usr/include

# Build directory for object files
OBJDIR = obj

# Sources live under src/<module>/; public headers under include/ (a vendored
# musl-compatible tree), private ones under src/internal/.
SRCS_C != find src -type f -name '*.c'
# Assembly sources are per-target: the dyld stub binder is Mach-O only, the
# program-interpreter entry is ELF only. Neither takes C headers or flags.
SRCS_ASM_MHO != find src/macho -type f -name '*.asm'
SRCS_ASM_ELF != find src/ldso -type f -name '*.asm'
# Preprocessed assembly (clone trampoline): shared source, assembled per
# target with clang so -D__APPLE__/symbol prefixes come out right.
SRCS_S != find src/thread -type f -name '*.S'
HDRS != find include src -type f -name '*.h'

# Include paths: public headers first, then the tree private headers resolve
# "internal/..." against.
INCLUDE_DIRS = -I${.CURDIR}/include -I${.CURDIR}/src

# --- Mach-O Configuration ---
LIB_OUTPUT_MHO = libc.dylib
# Prepend OBJDIR/ to every source file and change the suffix.
OBJS_MHO_C = ${SRCS_C_MHO:S/^/${OBJDIR}\//:.c=.mho.o}
OBJS_MHO_ASM = ${SRCS_ASM_MHO:S/^/${OBJDIR}\//:.asm=.mho.o}
OBJS_MHO_S = ${SRCS_S:S/^/${OBJDIR}\//:.S=.mho.o}
OBJS_MHO = ${OBJS_MHO_C} ${OBJS_MHO_ASM} ${OBJS_MHO_S}
EXPORTS_MHO = libc_mho.exports
INSTALL_NAME_MHO = /lib/${LIB_OUTPUT_MHO}

# --- ELF Configuration ---
# The Mach-O-only stub (dyld_stub_binder) has no business in an ELF link, so
# only C sources go into the .so.
LIB_OUTPUT_ELF = libc.so
OBJS_ELF_C = ${SRCS_C:S/^/${OBJDIR}\//:.c=.elf.o}
OBJS_ELF_ASM = ${SRCS_ASM_ELF:S/^/${OBJDIR}\//:.asm=.elf.o}
OBJS_ELF_S = ${SRCS_S:S/^/${OBJDIR}\//:.S=.elf.o}
OBJS_ELF = ${OBJS_ELF_C} ${OBJS_ELF_ASM} ${OBJS_ELF_S}
INSTALL_NAME_ELF = /lib/${LIB_OUTPUT_ELF}

# Common CFLAGS for both architectures
# NOTE: gnu17, not c17. The vendored musl headers gate the contemporary API
# (strdup, strnlen, stpcpy, ...) behind _BSD_SOURCE/_GNU_SOURCE, which
# <features.h> only auto-enables when __STRICT_ANSI__ is unset. Strict modes
# would hide declarations musl-built code takes for granted.
# The vendored musl sources/headers build warning-free under musl's own `-w`
# but trip two stylistic -Wextra families (unparenthesized endian.h macros);
# silence exactly those instead of weakening -Werror everywhere else.
# -D_GNU_SOURCE exposes the full musl API surface (gettid, strchrnul,
# memrchr, ...) that musl-built code takes for granted.
BASE_CFLAGS = -std=gnu17 -D_GNU_SOURCE -ffreestanding -fno-builtin \
          -fno-stack-protector -fPIC -fno-omit-frame-pointer -fno-lto \
          -nostdlib -Wall -Wextra -Werror -c \
          -Wno-bitwise-op-parentheses -Wno-shift-op-parentheses

# Specific Compiler & Linker Flags
CFLAGS_MHO = --target=${TARGET_TRIPLE_MHO} ${BASE_CFLAGS}
LDFLAGS_MHO = --target=${TARGET_TRIPLE_MHO} -fuse-ld=lld -nostdlib -dynamiclib \
          -Wl,-install_name,${INSTALL_NAME_MHO} -Wl,-arch,x86_64 \
          -Wl,-platform_version,macos,10.4,10.4 \
          -Wl,-exported_symbols_list,${EXPORTS_MHO}

CFLAGS_ELF = --target=${TARGET_TRIPLE_ELF} ${BASE_CFLAGS}
# NOTE: -shared must go through -Wl,: this clang driver accepts the bare flag
# but never forwards it to lld, which then emits an ET_EXEC instead of a .so.
LDFLAGS_ELF = --target=${TARGET_TRIPLE_ELF} -fuse-ld=lld -nostdlib -Wl,-shared \
          -Wl,-soname,${INSTALL_NAME_ELF} -Wl,-e,_dlstart

# Verbatim musl sources are warning-clean under musl's own `-w` but not under
# our -Wextra: strstr.c's two-way matcher mixes signs, and src/math trips
# conversion warnings by design (it bit-twiddles floats). Those files get
# targeted suppressions; everything written for this libc keeps full -Werror.
VENDORED_C = ${SRCS_C:Msrc/string/*} ${SRCS_C:Msrc/stdlib/atoi.c} \
            ${SRCS_C:Msrc/stdlib/qsort*.c} ${SRCS_C:Msrc/search/t*.c} \
            ${SRCS_C:Msrc/math/*}
OWN_C = ${SRCS_C:Nsrc/string/*:Nsrc/stdlib/atoi.c:Nsrc/stdlib/qsort*.c:Nsrc/search/t*.c:Nsrc/math/*}
# The interpreter (src/ldso) is ELF-only: a dylib is entered by the kernel,
# never by ourself, so its objects stay out of the Mach-O half.
SRCS_C_MHO = ${SRCS_C:Nsrc/ldso/*}
VENDORED_C_MHO = ${VENDORED_C:Nsrc/ldso/*}
OWN_C_MHO = ${OWN_C:Nsrc/ldso/*}
# -Dhidden= neutralizes musl's visibility annotations in data tables (the
# adapted src/internal/libm.h does the same for sources that include it).
# -Wno-macro-redefined: one FreeBSD-origin math file re-#defines _GNU_SOURCE.
CFLAGS_VENDORED_MHO = ${CFLAGS_MHO} -Wno-sign-compare -Wno-unused-but-set-variable -Wno-macro-redefined -Dhidden=
CFLAGS_VENDORED_ELF = ${CFLAGS_ELF} -Wno-sign-compare -Wno-unused-but-set-variable -Wno-macro-redefined -Dhidden=


.PHONY: all mho elf static compile install clean

# Default target does both: `hmake` with no arguments builds libc.dylib and
# libc.so from the same sources.
all: mho elf

# Kept for callers that drive `-C src/libs/libc compile` (macho-test does).
compile: all

mho: ${LIB_OUTPUT_MHO}

elf: ${LIB_OUTPUT_ELF}

# Static archive of the ELF objects. In-tree ELF software links -Bstatic by
# default (musl's libc.a is what -lc resolves to there), so testing our libc
# in place of musl's needs this alongside the .so. Deliberately outside the
# default `all`: static linking is a test/replacement artifact, the dylib and
# the .so are the products.
LIB_OUTPUT_STATIC = libc.a

static: ${OBJS_ELF}
	llvm-ar rcs ${LIB_OUTPUT_STATIC} ${OBJS_ELF}


# ==========================================
# Object Compilation Rules
# ==========================================
# Vendored (verbatim musl) sources compile with the relaxed flags; everything
# written for this libc keeps the strict ones. Mach-O rules come from the
# _MHO lists (no interpreter objects); ELF rules from the full lists.
.for src in ${VENDORED_C_MHO}
${OBJDIR}/${src:.c=.mho.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_VENDORED_MHO} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
.endfor

.for src in ${VENDORED_C}
${OBJDIR}/${src:.c=.elf.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_VENDORED_ELF} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
.endfor

.for src in ${OWN_C_MHO}
${OBJDIR}/${src:.c=.mho.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_MHO} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
.endfor

.for src in ${OWN_C}
${OBJDIR}/${src:.c=.elf.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_ELF} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
.endfor

# Preprocessed assembly builds with the C compiler (not nasm); one rule per
# target triple. Strict flags stay: this file is ours, not vendored.
.for src in ${SRCS_S}
${OBJDIR}/${src:.S=.mho.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_MHO} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
${OBJDIR}/${src:.S=.elf.o}: ${src} ${HDRS}
	@mkdir -p $$(dirname ${.TARGET})
	${COMPILER} ${CFLAGS_ELF} ${INCLUDE_DIRS} -o ${.TARGET} ${src}
.endfor

# NASM sources assemble with nasm directly; they take no C headers or flags.
.for src in ${SRCS_ASM_MHO}
${OBJDIR}/${src:.asm=.mho.o}: ${src}
	@mkdir -p $$(dirname ${.TARGET})
	${NASM} ${NASMFLAGS_MHO} -o ${.TARGET} ${src}
.endfor

.for src in ${SRCS_ASM_ELF}
${OBJDIR}/${src:.asm=.elf.o}: ${src}
	@mkdir -p $$(dirname ${.TARGET})
	${NASM} ${NASMFLAGS_ELF} -o ${.TARGET} ${src}
.endfor


# ==========================================
# Mach-O Build Rules
# ==========================================
${EXPORTS_MHO}: ${OBJS_MHO}
	llvm-nm --defined-only -g ${.ALLSRC} | \
	    awk '$$2 ~ /^[TDBRSC]$$/ { print $$3 }' | sort -u > ${.TARGET}

${LIB_OUTPUT_MHO}: ${OBJS_MHO} ${EXPORTS_MHO}
	${COMPILER} ${LDFLAGS_MHO} ${OBJS_MHO} -o ${.TARGET}


# ==========================================
# ELF Build Rules
# ==========================================
${LIB_OUTPUT_ELF}: ${OBJS_ELF}
	${COMPILER} ${LDFLAGS_ELF} ${OBJS_ELF} -o ${.TARGET}


# ==========================================
# Install & Clean
# ==========================================
# Headers install as a tree (bits/, sys/, ...) - never flattened - so that
# #include <sys/mman.h> and friends resolve exactly like they do against musl.
# NOTE: installing libc.so into ${LIBDIR} puts it ahead of musl's libc.so on
# the ELF link path (-L${LIBDIR} precedes -L/usr/lib in software Makefiles),
# so -lc would then resolve to this minimal libc. That is only appropriate for
# binaries written against this libc's coverage (see README.md); the default
# disk flow deliberately builds without installing for this reason.
install: all
	sudo mkdir -p ${LIBDIR} ${INCLUDEDIR}
	sudo cp ${LIB_OUTPUT_MHO} ${LIB_OUTPUT_ELF} ${LIBDIR}/
	sudo cp -R ${.CURDIR}/include/. ${INCLUDEDIR}/

clean:
	rm -f ${LIB_OUTPUT_MHO} ${EXPORTS_MHO}
	rm -f ${LIB_OUTPUT_ELF} ${LIB_OUTPUT_STATIC}
	rm -rf ${OBJDIR}
