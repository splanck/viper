#!/bin/bash
#===----------------------------------------------------------------------===#
#
# Part of the Zanna project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: src/tests/e2e/test_xenoscape_native_start.sh
# Purpose: Exercise Xenoscape's native ARM64 start and new-game save paths at O2.
# Key invariants: Both frontend and backend use O2; save coverage uses an
#                 isolated persistent store.
# Ownership/Lifetime: Owns one temporary directory removed by the EXIT trap.
# Links: examples/games/xenoscape/start_probe.zia,
#        examples/games/xenoscape/native_new_game_save_probe.zia
#
#===----------------------------------------------------------------------===#

set -euo pipefail

ZANNA_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/xeno_native_start.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/start_probe.il"
BIN_FILE="$TMP_DIR/start_probe"
SAVE_IL_FILE="$TMP_DIR/native_new_game_save_probe.il"
SAVE_BIN_FILE="$TMP_DIR/native_new_game_save_probe"

"$ZANNA_BIN" build "$ROOT_DIR/examples/games/xenoscape/start_probe.zia" -O2 -o "$IL_FILE"
"$ZANNA_BIN" codegen arm64 "$IL_FILE" --native-asm --native-link \
    --skip-il-optimization -O2 -o "$BIN_FILE"

OUTPUT="$("$BIN_FILE")"
printf '%s\n' "$OUTPUT"

grep -q '^DIRECT_MOVE$' <<<"$OUTPUT"
grep -q '^moveX=19200$' <<<"$OUTPUT"
grep -q '^moveY=83229$' <<<"$OUTPUT"
grep -q '^FRAME=0$' <<<"$OUTPUT"
grep -q '^rawX=19200$' <<<"$OUTPUT"
grep -q '^cameraX=0$' <<<"$OUTPUT"
if grep -q '^cameraX=8320$' <<<"$OUTPUT"; then
    echo "Unexpected end-of-level camera clamp in native-linked Xenoscape start path" >&2
    exit 1
fi

"$ZANNA_BIN" build "$ROOT_DIR/examples/games/xenoscape/native_new_game_save_probe.zia" \
    -O2 -o "$SAVE_IL_FILE"
"$ZANNA_BIN" codegen arm64 "$SAVE_IL_FILE" --native-asm --native-link \
    --skip-il-optimization -O2 -o "$SAVE_BIN_FILE"

SAVE_OUTPUT="$("$SAVE_BIN_FILE")"
printf '%s\n' "$SAVE_OUTPUT"
grep -q '^SAVE_OK$' <<<"$SAVE_OUTPUT"

echo "PASS"
