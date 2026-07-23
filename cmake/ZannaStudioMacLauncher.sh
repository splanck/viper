#!/bin/sh
#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: cmake/ZannaStudioMacLauncher.sh
# Purpose: Preserve the `zannastudio` command while starting the macOS payload
#          with the authored executable name Cocoa exposes in the menu bar.
# Key invariants:
#   - The native payload is a sibling named exactly `Zanna Studio`.
#   - Installed command symlinks are resolved before locating that sibling.
#   - All arguments and the payload's exit status pass through unchanged.
# Ownership/Lifetime:
#   - CMake and build_ide.sh copy this launcher into generated output trees.
# Cross-platform touchpoints: Installed and executed on macOS only.
# Links: docs/adr/0149-macos-zanna-studio-application-identity.md
#
#===----------------------------------------------------------------------===#

set -eu

launcher_path=$0
case "$launcher_path" in
    */*) ;;
    *) launcher_path=$(command -v "$launcher_path") ;;
esac
case "$launcher_path" in
    /*) ;;
    *) launcher_path="$(pwd)/$launcher_path" ;;
esac

link_hops=0
while [ -L "$launcher_path" ]; do
    link_hops=$((link_hops + 1))
    if [ "$link_hops" -gt 32 ]; then
        echo "zannastudio: launcher symlink chain is too deep" >&2
        exit 126
    fi
    link_target=$(readlink "$launcher_path")
    case "$link_target" in
        /*) launcher_path=$link_target ;;
        *) launcher_path="$(dirname "$launcher_path")/$link_target" ;;
    esac
done

launcher_dir=$(dirname "$launcher_path")
launcher_dir=$(CDPATH= cd -P "$launcher_dir" && pwd)
exec "$launcher_dir/Zanna Studio" "$@"
