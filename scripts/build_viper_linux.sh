#!/usr/bin/env bash
# build_viper_linux.sh — Linux compatibility wrapper for build_viper_unix.sh.
set -euo pipefail

if [[ "$(uname -s 2>/dev/null)" != "Linux" ]]; then
    echo "Error: build_viper_linux.sh must be run on Linux"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/build_viper_unix.sh" "$@"
