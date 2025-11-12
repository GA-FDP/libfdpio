#!/bin/bash

set -euxo pipefail

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake -DCMAKE_INSTALL_PREFIX=$PREFIX ..

# Build
make

# Install
make install
