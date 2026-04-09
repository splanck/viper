#!/usr/bin/env bash
# build_viper_mac.sh — macOS compatibility wrapper for build_viper_unix.sh.
set -euo pipefail

if [[ "$(uname -s 2>/dev/null)" != "Darwin" ]]; then
    echo "Error: build_viper_mac.sh must be run on macOS"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/build_viper_unix.sh" "$@"
