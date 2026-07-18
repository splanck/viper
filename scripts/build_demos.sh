#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: scripts/build_demos.sh
# Purpose: Dispatch the demo build to the native platform entry point.
# Key invariants:
#   - Windows dispatch uses the canonical PowerShell implementation.
#   - All user arguments are forwarded without reinterpretation.
# Ownership/Lifetime:
#   - This process is replaced by the selected platform build script.
# Cross-platform touchpoints:
#   - Uses uname for host selection and cygpath for Windows path conversion.
# Links: build_demos_linux.sh, build_demos_mac.sh, build_demos_win.ps1
#
#===----------------------------------------------------------------------===#

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
        POWERSHELL_EXE=""
        if command -v pwsh.exe >/dev/null 2>&1; then
            POWERSHELL_EXE="pwsh.exe"
        elif command -v powershell.exe >/dev/null 2>&1; then
            POWERSHELL_EXE="powershell.exe"
        fi
        if [[ -n "$POWERSHELL_EXE" ]]; then
            PS_SCRIPT="$SCRIPT_DIR/build_demos_win.ps1"
            if command -v cygpath >/dev/null 2>&1; then
                PS_SCRIPT="$(cygpath -aw "$PS_SCRIPT")"
            fi
            exec "$POWERSHELL_EXE" -NoProfile -ExecutionPolicy Bypass -File "$PS_SCRIPT" "$@"
        fi
        echo "Error: PowerShell is required; use scripts/build_demos_win.ps1 on Windows"
        exit 1
        ;;
    *)
        echo "Error: unsupported platform"
        exit 1
        ;;
esac
