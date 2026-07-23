#!/bin/bash
#===----------------------------------------------------------------------===//
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# File: tests/e2e/test_zannastudio_basic_query_native.sh
# Purpose: Compile and run the Studio BASIC query worker probe natively.
# Key invariants:
#   - The generated binary uses the host's supported native architecture.
#   - Temporary IL and executable outputs are removed on every exit path.
# Ownership/Lifetime: Standalone CTest wrapper owning one temporary directory.
# Links: zannastudio/src/probes/basic_query_job_probe.zia
#
#===----------------------------------------------------------------------===//

set -euo pipefail

ZANNA_BIN="$1"
ROOT_DIR="$2"
ARCH="${3:-${ZANNA_NATIVE_TEST_ARCH:-}}"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zannastudio_basic_query_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/basic_query_job_probe.il"
BIN_FILE="$TMP_DIR/basic_query_job_probe"

if [[ -z "$ARCH" ]]; then
    case "$(uname -m)" in
        arm64|aarch64)
            ARCH="arm64"
            ;;
        x86_64|amd64)
            ARCH="x64"
            ;;
        *)
            echo "Unsupported native test architecture: $(uname -m)" >&2
            exit 1
            ;;
    esac
fi

"$ZANNA_BIN" build \
    "$ROOT_DIR/src/zannastudio/src/probes/basic_query_job_probe.zia" \
    -o "$IL_FILE"
"$ZANNA_BIN" codegen "$ARCH" "$IL_FILE" --native-asm --native-link -O1 -o "$BIN_FILE"

OUTPUT="$("$BIN_FILE")"
printf '%s\n' "$OUTPUT"

grep -q '^RESULT: ok$' <<<"$OUTPUT"
echo "PASS"
