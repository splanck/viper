#!/usr/bin/env bash
# build_viper.sh — cross-platform Viper build + test + install script.
# Works on macOS, Linux, and Windows (Git Bash / MSYS2 / WSL).
set -e

# ── OS and compiler detection ────────────────────────────────────────────────
case "$(uname -s 2>/dev/null)" in
    Darwin)                  OS="macos" ;;
    Linux)                   OS="linux" ;;
    MINGW*|MSYS*|CYGWIN*)    OS="windows" ;;
    *)                       OS="unknown" ;;
esac

# Number of parallel jobs: nproc (Linux), sysctl (macOS), env var (Windows).
if   command -v nproc    &>/dev/null; then JOBS=$(nproc)
elif command -v sysctl   &>/dev/null; then JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 8)
else                                       JOBS="${NUMBER_OF_PROCESSORS:-8}"
fi

# Compiler selection: prefer clang on Unix; let CMake auto-detect on Windows.
if [ "$OS" = "windows" ]; then
    # Prefer clang-cl (LLVM for Windows) if available; else let CMake pick MSVC.
    if command -v clang-cl &>/dev/null; then
        COMPILER_FLAGS="-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
    else
        COMPILER_FLAGS=""
    fi
else
    if command -v clang &>/dev/null; then
        COMPILER_FLAGS="-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
    elif command -v gcc &>/dev/null; then
        COMPILER_FLAGS="-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++"
    else
        COMPILER_FLAGS=""
    fi
fi

# ── Build ────────────────────────────────────────────────────────────────────
cmake --build build --target clean-all 2>/dev/null || true
# shellcheck disable=SC2086
cmake -S . -B build $COMPILER_FLAGS
cmake --build build -j"$JOBS"

# Flush filesystem I/O before running tests.
# sync(1) is POSIX-only; cmake -E sleep 0 is a portable no-op flush alternative.
if command -v sync &>/dev/null; then
    sync
    sleep 1
else
    cmake -E sleep 1
fi

# ── Test ─────────────────────────────────────────────────────────────────────
rm -rf build/Testing
ctest --test-dir build --output-on-failure -j"$JOBS"

# ── Install ──────────────────────────────────────────────────────────────────
if [ "$OS" = "windows" ]; then
    # Windows: install to user-local directory (no sudo).
    INSTALL_PREFIX="${LOCALAPPDATA:-$HOME}/viper"
    cmake --install build --prefix "$INSTALL_PREFIX"
    echo "Installed to $INSTALL_PREFIX"
elif [ "$(id -u)" -eq 0 ]; then
    # Already root.
    cmake --install build --prefix /usr/local
else
    sudo cmake --install build --prefix /usr/local
fi
