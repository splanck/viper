#!/usr/bin/env bash
# build_viper_unix.sh — canonical POSIX Viper build + test + install script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
PLATFORM="$(uname -s 2>/dev/null || true)"

case "$PLATFORM" in
    Darwin)
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
        ;;
    Linux)
        if command -v nproc >/dev/null 2>&1; then
            JOBS="$(nproc)"
        else
            JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)"
        fi
        ;;
    *)
        echo "Error: build_viper_unix.sh supports macOS and Linux only"
        exit 1
        ;;
esac

BUILD_DIR="${VIPER_BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${VIPER_BUILD_TYPE:-Debug}"
SKIP_INSTALL="${VIPER_SKIP_INSTALL:-0}"
SKIP_AUDIT="${VIPER_SKIP_AUDIT:-0}"
SKIP_LINT="${VIPER_SKIP_LINT:-0}"
SKIP_SMOKE="${VIPER_SKIP_SMOKE:-0}"
INSTALL_PREFIX="${VIPER_INSTALL_PREFIX:-/usr/local}"
CONFIGURE_ARGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
)

if [[ -n "${VIPER_CMAKE_GENERATOR:-}" ]]; then
    CONFIGURE_ARGS+=(-G "${VIPER_CMAKE_GENERATOR}")
fi

if command -v clang >/dev/null 2>&1; then
    CONFIGURE_ARGS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
elif command -v gcc >/dev/null 2>&1; then
    CONFIGURE_ARGS+=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++)
fi

if [[ -n "${VIPER_EXTRA_CMAKE_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    EXTRA_ARGS=( ${VIPER_EXTRA_CMAKE_ARGS} )
    CONFIGURE_ARGS+=("${EXTRA_ARGS[@]}")
fi

cmake --build "$BUILD_DIR" --target clean-all 2>/dev/null || true
cmake "${CONFIGURE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$JOBS"

if command -v sync >/dev/null 2>&1; then
    sync
    sleep 1
else
    cmake -E sleep 1
fi

rm -rf "$BUILD_DIR/Testing"
ctest --test-dir "$BUILD_DIR" --output-on-failure -j"$JOBS"

if [[ "$SKIP_LINT" != "1" && -x "$SCRIPT_DIR/lint_platform_policy.sh" ]]; then
    "$SCRIPT_DIR/lint_platform_policy.sh"
fi

if [[ "$SKIP_AUDIT" != "1" && -x "$SCRIPT_DIR/audit_runtime_surface.sh" ]]; then
    "$SCRIPT_DIR/audit_runtime_surface.sh" --build-dir="$BUILD_DIR"
fi

if [[ "$SKIP_SMOKE" != "1" && -x "$SCRIPT_DIR/run_cross_platform_smoke.sh" ]]; then
    "$SCRIPT_DIR/run_cross_platform_smoke.sh" --build-dir "$BUILD_DIR"
fi

if [[ "$SKIP_INSTALL" == "1" ]]; then
    echo "Skipping install (VIPER_SKIP_INSTALL=1)"
    exit 0
fi

if [[ "$(id -u)" -eq 0 ]]; then
    cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
else
    sudo cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
fi
