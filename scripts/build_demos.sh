#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "$(uname -s 2>/dev/null)" in
    Darwin)
        exec "$SCRIPT_DIR/build_demos_mac.sh" "$@"
        ;;
    Linux)
        exec "$SCRIPT_DIR/build_demos_linux.sh" "$@"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        if command -v cmd.exe >/dev/null 2>&1; then
            CMD_SCRIPT="$SCRIPT_DIR/build_demos.cmd"
            if command -v cygpath >/dev/null 2>&1; then
                CMD_SCRIPT="$(cygpath -aw "$CMD_SCRIPT")"
            fi
            if [[ $# -gt 0 ]]; then
                printf -v CMD_ARGS ' "%s"' "$@"
                exec cmd.exe //c "\"$CMD_SCRIPT\"$CMD_ARGS"
            fi
            exec cmd.exe //c "\"$CMD_SCRIPT\""
        fi
        echo "Error: use scripts/build_demos.cmd on Windows"
        exit 1
        ;;
    *)
        echo "Error: unsupported platform"
        exit 1
        ;;
esac
