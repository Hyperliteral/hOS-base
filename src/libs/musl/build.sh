#!/bin/bash
set -x # show cmds
set -e # fail globally

# Know where we at :p
SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

MUSL_RELEASE="musl-1.2.5"
TARGET="x86_64-unknown-elf"

cd "${SCRIPTPATH}"

# --noreplace -> won't re-compile if it finds libc
if test -f "$SCRIPTPATH/cavos-out/lib/libc.a"; then
    if [ "$#" -eq 1 ]; then
        exit 0
    fi
fi

# Ensure we have the sources!
if ! test -d "$SCRIPTPATH/$MUSL_RELEASE"; then
    git clone "https://github.com/hyperliteral/$MUSL_RELEASE.git" "$MUSL_RELEASE"
fi

# Ensure the cavOS toolchain's in PATH (if any cross-utils remain)
if [[ ":$PATH:" != *":$HOME/opt/cross/bin:"* ]]; then
    export PATH=$HOME/opt/cross/bin:$PATH
fi

# Configure output directory
export PREFIX="${SCRIPTPATH}/cavos-out"
mkdir -p cavos-build
cd cavos-build

# Configure using host Clang with lld linker, target triple, and LLVM utils
CC="clang --target=$TARGET -fuse-ld=lld" \
AR="llvm-ar" \
RANLIB="llvm-ranlib" \
../$MUSL_RELEASE/configure \
    --target="$TARGET" \
    --host="$TARGET" \
    --prefix="$PREFIX" \
    --syslibdir="/lib" \
    --enable-debug

make clean
make all -j$(nproc)
make install

# Copy libraries (and update headers) using sudo for restricted target directory
sudo mkdir -p "$SCRIPTPATH/../target/usr/"
sudo cp -r "$PREFIX/lib" "$PREFIX/include" "$SCRIPTPATH/../../../target/usr/"

# required for proper dynamic linking
sudo mkdir -p "$SCRIPTPATH/../../../target/lib/"
sudo cp "$SCRIPTPATH/../../../target/usr/lib/libc.so" "$SCRIPTPATH/../../../target/lib/ld64.so.1"