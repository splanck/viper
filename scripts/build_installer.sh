#!/usr/bin/env bash
# build_installer.sh — thin wrapper around `zanna install-package`.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${ZANNA_BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${ZANNA_BUILD_TYPE:-Release}"

case "$(uname -s 2>/dev/null)" in
    Darwin)
        BUILD_SCRIPT="$SCRIPT_DIR/build_zanna_mac.sh"
        ;;
    Linux)
        BUILD_SCRIPT="$SCRIPT_DIR/build_zanna_linux.sh"
        ;;
    *)
        echo "Error: use scripts/build_installer.cmd on Windows"
        exit 1
        ;;
esac

export ZANNA_BUILD_DIR="$BUILD_DIR"
export ZANNA_BUILD_TYPE="$BUILD_TYPE"
export ZANNA_SKIP_INSTALL="${ZANNA_SKIP_INSTALL:-1}"
FORWARD_ARGS=("$@")
USES_EXISTING_INPUT=0
HAS_EXPLICIT_BUILD_DIR=0
for ((i = 0; i < ${#FORWARD_ARGS[@]}; ++i)); do
    case "${FORWARD_ARGS[i]}" in
        --stage-dir|--verify-only)
            USES_EXISTING_INPUT=1
            ;;
        --build-dir)
            HAS_EXPLICIT_BUILD_DIR=1
            ;;
    esac
done

if [[ "$USES_EXISTING_INPUT" -eq 0 ]]; then
    if [[ "${ZANNA_EXTRA_CMAKE_ARGS:-}" != *"-DZANNA_INSTALL_ZANNAIDE="* ]]; then
        export ZANNA_EXTRA_CMAKE_ARGS="${ZANNA_EXTRA_CMAKE_ARGS:-} -DZANNA_INSTALL_ZANNAIDE=ON"
    fi
    "$BUILD_SCRIPT"
fi

ZANNA_EXE="$BUILD_DIR/src/tools/zanna/zanna"
if [[ ! -x "$ZANNA_EXE" ]]; then
    echo "Error: zanna executable not found at $ZANNA_EXE" >&2
    echo "Build Zanna first or set ZANNA_BUILD_DIR to an existing build tree." >&2
    exit 1
fi

if [[ "$USES_EXISTING_INPUT" -eq 0 && "$HAS_EXPLICIT_BUILD_DIR" -eq 0 ]]; then
    FORWARD_ARGS=(--build-dir "$BUILD_DIR" --skip-build "${FORWARD_ARGS[@]}")
fi

exec "$ZANNA_EXE" install-package "${FORWARD_ARGS[@]}"
