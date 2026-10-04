#!/usr/bin/env bash

# Enable command printing and exit immediately on command failure
set -x
set -e

# Clone the repository if the libc directory does not exist
if [ ! -d "libc" ]; then
    git clone https://github.com/hyperliteral/libc.git
fi

# Navigate into libc and run the build tool
cd libc
hmake
hmake install