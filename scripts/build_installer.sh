#!/usr/bin/env bash
# build_installer.sh — thin wrapper around `viper install-package`.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${VIPER_BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${VIPER_BUILD_TYPE:-Debug}"

case "$(uname -s 2>/dev/null)" in
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
        echo "Error: use scripts/build_installer.cmd on Windows"
        exit 1
        ;;
esac

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    CONFIGURE_ARGS=(-S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
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
    cmake "${CONFIGURE_ARGS[@]}"
fi

cmake --build "$BUILD_DIR" -j"$JOBS" --target viper

FORWARD_ARGS=("$@")
HAS_STAGE_MODE=0
for ((i = 0; i < ${#FORWARD_ARGS[@]}; ++i)); do
    case "${FORWARD_ARGS[i]}" in
        --build-dir|--stage-dir|--verify-only)
            HAS_STAGE_MODE=1
            ;;
    esac
done

if [[ "$HAS_STAGE_MODE" -eq 0 ]]; then
    FORWARD_ARGS=(--build-dir "$BUILD_DIR" "${FORWARD_ARGS[@]}")
fi

exec "$BUILD_DIR/src/tools/viper/viper" install-package "${FORWARD_ARGS[@]}"
