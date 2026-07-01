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
if [[ "${VIPER_EXTRA_CMAKE_ARGS:-}" != *"-DVIPER_INSTALL_VIPERIDE="* ]]; then
    export VIPER_EXTRA_CMAKE_ARGS="${VIPER_EXTRA_CMAKE_ARGS:-} -DVIPER_INSTALL_VIPERIDE=ON"
fi
"$BUILD_SCRIPT"

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
    FORWARD_ARGS=(--build-dir "$BUILD_DIR" --skip-build "${FORWARD_ARGS[@]}")
fi

exec "$BUILD_DIR/src/tools/viper/viper" install-package "${FORWARD_ARGS[@]}"
