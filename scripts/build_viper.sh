#!/usr/bin/env bash
# build_viper.sh — compatibility wrapper that dispatches to the platform script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$(uname -s 2>/dev/null)" in
    Darwin)
        exec "$SCRIPT_DIR/build_viper_mac.sh" "$@"
        ;;
    Linux)
        exec "$SCRIPT_DIR/build_viper_linux.sh" "$@"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        if command -v cmd.exe >/dev/null 2>&1; then
            CMD_SCRIPT="$SCRIPT_DIR/build_viper.cmd"
            if command -v cygpath >/dev/null 2>&1; then
                CMD_SCRIPT="$(cygpath -aw "$CMD_SCRIPT")"
            fi
            exec cmd.exe //c "$CMD_SCRIPT"
        fi
        echo "Error: use scripts/build_viper.cmd on Windows"
        exit 1
        ;;
    *)
        echo "Error: unsupported platform"
        exit 1
        ;;
esac
