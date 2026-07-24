#!/usr/bin/env bash
#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/e2e/test_linux_build_wrapper_symlink.sh
# Purpose: Verify the Linux build wrapper resolves symlinks and spaced paths.
#
# Key invariants:
#   - The wrapper delegates to the repository's real Unix build driver.
#   - The test replaces exec and never starts a build.
#
# Ownership/Lifetime:
#   - A temporary directory and symlink are removed on exit.
#
# Links: scripts/build_zanna_linux.sh
#
#===----------------------------------------------------------------------===#

set -euo pipefail

repository_root="$1"
temporary_root="$(mktemp -d)"
trap 'rm -rf -- "$temporary_root"' EXIT

link_directory="$temporary_root/path with spaces"
mkdir -p -- "$link_directory"
wrapper_link="$link_directory/linux build wrapper"
ln -s -- "$repository_root/scripts/build_zanna_linux.sh" "$wrapper_link"

expected_delegate="$repository_root/scripts/build_zanna_unix.sh"
observed_delegate=""

uname() {
    printf 'Linux\n'
}

exec() {
    observed_delegate="$1"
}

source "$wrapper_link" --sentinel

[[ "$observed_delegate" == "$expected_delegate" ]]
printf 'PASS\n'
