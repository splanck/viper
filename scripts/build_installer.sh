#!/usr/bin/env bash
# build_installer.sh — thin wrapper around `viper install-package`.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${VIPER_BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${VIPER_BUILD_TYPE:-Release}"

case "$(uname -s 2>/dev/null)" in
    Darwin)
        BUILD_SCRIPT="$SCRIPT_DIR/build_viper_mac.sh"
        ;;
    Linux)
        BUILD_SCRIPT="$SCRIPT_DIR/build_viper_linux.sh"
        ;;
    *)
        echo "Error: use scripts/build_installer.cmd on Windows"
        exit 1
        ;;
esac

export VIPER_BUILD_DIR="$BUILD_DIR"
export VIPER_BUILD_TYPE="$BUILD_TYPE"
export VIPER_SKIP_INSTALL="${VIPER_SKIP_INSTALL:-1}"
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
    if [[ "${VIPER_EXTRA_CMAKE_ARGS:-}" != *"-DVIPER_INSTALL_VIPERIDE="* ]]; then
        export VIPER_EXTRA_CMAKE_ARGS="${VIPER_EXTRA_CMAKE_ARGS:-} -DVIPER_INSTALL_VIPERIDE=ON"
    fi
    "$BUILD_SCRIPT"
fi

VIPER_EXE="$BUILD_DIR/src/tools/viper/viper"
if [[ ! -x "$VIPER_EXE" ]]; then
    echo "Error: viper executable not found at $VIPER_EXE" >&2
    echo "Build Viper first or set VIPER_BUILD_DIR to an existing build tree." >&2
    exit 1
fi

if [[ "$USES_EXISTING_INPUT" -eq 0 && "$HAS_EXPLICIT_BUILD_DIR" -eq 0 ]]; then
    FORWARD_ARGS=(--build-dir "$BUILD_DIR" --skip-build "${FORWARD_ARGS[@]}")
fi

exec "$VIPER_EXE" install-package "${FORWARD_ARGS[@]}"
