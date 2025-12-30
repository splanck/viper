#!/bin/bash
set -e

cmake --build build --target clean-all
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j8

# Sync filesystem after build - fixes ctest parallel slowdown on macOS
# (filesystem I/O from build interferes with ctest process spawning)
sync
sleep 1

# Clear test cache and run tests
rm -rf build/Testing
ctest --test-dir build --output-on-failure -j8

sudo cmake --install build --prefix /usr/local
