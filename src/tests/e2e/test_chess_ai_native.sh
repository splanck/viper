#!/bin/bash
set -euo pipefail

VIPER_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/chess_ai_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

BIN_FILE="$TMP_DIR/chess_ai_thread_probe"

"$VIPER_BIN" build "$ROOT_DIR/examples/games/chess/ai_thread_probe.zia" -O1 -o "$BIN_FILE"

OUTPUT="$("$BIN_FILE")"
printf '%s\n' "$OUTPUT"

grep -q '^RESULT: ok$' <<<"$OUTPUT"

echo "PASS"
