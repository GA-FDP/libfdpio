#!/bin/bash

set -euxo pipefail

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DCMAKE_INSTALL_LIBDIR=lib \
    ..

# Build
make -j${CPU_COUNT}

# Install
make install
