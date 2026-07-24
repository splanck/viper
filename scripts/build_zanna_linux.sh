#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/build_zanna_linux.sh
# Purpose: Linux compatibility wrapper for the shared Unix build driver.
#
# Key invariants:
#   - Invocation is independent of the caller's working directory.
#   - Symlinked wrapper paths resolve to the repository's real scripts directory.
#
# Ownership/Lifetime:
#   - Replaces itself with build_zanna_unix.sh; retains no process state.
#
# Links: scripts/build_zanna_unix.sh
#
#===----------------------------------------------------------------------===#

set -euo pipefail

if [[ "$(uname -s 2>/dev/null)" != "Linux" ]]; then
    echo "Error: build_zanna_linux.sh must be run on Linux"
    exit 1
fi

SCRIPT_PATH="$(readlink -f -- "${BASH_SOURCE[0]}")"
if [[ -z "$SCRIPT_PATH" ]]; then
    echo "Error: could not resolve build_zanna_linux.sh"
    exit 1
fi
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"
exec "$SCRIPT_DIR/build_zanna_unix.sh" "$@"
