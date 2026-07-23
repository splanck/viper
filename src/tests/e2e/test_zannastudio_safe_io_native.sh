#!/bin/bash
#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: tests/e2e/test_zannastudio_safe_io_native.sh
# Purpose: Build and run Studio's checked text-I/O regression under optimized
#          native code generation.
# Key invariants:
#   - Native string values returned through try/catch retain valid ownership.
#   - Missing and oversized files report checked failures without trapping.
#   - Serialized exception-handling IL verifies and code-generates identically.
# Ownership/Lifetime:
#   - The script owns its temporary IL, native binaries, and captured output.
#   - An EXIT trap removes the complete temporary directory on every path.
# Links: zannastudio/src/probes/safe_io_native_probe.zia
#
#===----------------------------------------------------------------------===#

set -euo pipefail

ZANNA_BIN="$1"
ROOT_DIR="$2"
NATIVE_ARCH="${3:-${ZANNA_NATIVE_TEST_ARCH:-}}"
if [[ -z "$NATIVE_ARCH" ]]; then
    case "$(uname -m)" in
        arm64 | aarch64) NATIVE_ARCH="arm64" ;;
        x86_64 | amd64) NATIVE_ARCH="x64" ;;
        *)
            echo "unsupported native test architecture: $(uname -m)" >&2
            exit 1
            ;;
    esac
fi

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zannastudio_safe_io_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

for OPT_LEVEL in 1 2; do
    BIN_FILE="$TMP_DIR/zannastudio_safe_io_probe_o${OPT_LEVEL}"
    if [[ "$OPT_LEVEL" == "2" ]]; then
        IL_FILE="$TMP_DIR/zannastudio_safe_io_probe_o2.il"
        "$ZANNA_BIN" build "$ROOT_DIR/src/zannastudio/src/probes/safe_io_native_probe.zia" \
            -O2 -o "$IL_FILE"
        "$ZANNA_BIN" codegen "$NATIVE_ARCH" "$IL_FILE" -O2 -o "$BIN_FILE"
    else
        "$ZANNA_BIN" build "$ROOT_DIR/src/zannastudio/src/probes/safe_io_native_probe.zia" \
            "-O${OPT_LEVEL}" -o "$BIN_FILE"
    fi
    OUTPUT="$("$BIN_FILE")"
    printf '%s\n' "$OUTPUT"
    grep -q '^RESULT: ok$' <<<"$OUTPUT"
done

echo "PASS"
