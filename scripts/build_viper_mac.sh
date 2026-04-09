#!/usr/bin/env bash
# build_viper_mac.sh — macOS Viper build + test + install script.
set -euo pipefail

if [[ "$(uname -s 2>/dev/null)" != "Darwin" ]]; then
    echo "Error: build_viper_mac.sh must be run on macOS"
    exit 1
fi

SKIP_INSTALL="${VIPER_SKIP_INSTALL:-0}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
COMPILER_FLAGS=()

if command -v clang >/dev/null 2>&1; then
    COMPILER_FLAGS=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
elif command -v gcc >/dev/null 2>&1; then
    COMPILER_FLAGS=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++)
fi

cmake --build build --target clean-all 2>/dev/null || true
cmake -S . -B build "${COMPILER_FLAGS[@]}"
cmake --build build -j"$JOBS"

if command -v sync >/dev/null 2>&1; then
    sync
    sleep 1
else
    cmake -E sleep 1
fi

rm -rf build/Testing
ctest --test-dir build --output-on-failure -j"$JOBS"

if [[ "$SKIP_INSTALL" == "1" ]]; then
    echo "Skipping install (VIPER_SKIP_INSTALL=1)"
    exit 0
fi

if [[ "$(id -u)" -eq 0 ]]; then
    cmake --install build --prefix /usr/local
else
    sudo cmake --install build --prefix /usr/local
fi
