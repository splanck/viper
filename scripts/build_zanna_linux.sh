#!/usr/bin/env bash
# build_zanna_linux.sh — Linux compatibility wrapper for build_zanna_unix.sh.
set -euo pipefail

if [[ "$(uname -s 2>/dev/null)" != "Linux" ]]; then
    echo "Error: build_zanna_linux.sh must be run on Linux"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/build_zanna_unix.sh" "$@"
