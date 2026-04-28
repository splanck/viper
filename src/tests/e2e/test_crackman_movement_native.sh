#!/bin/bash
set -euo pipefail

VIPER_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/crackman_movement_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/movement_probe.il"
BIN_FILE="$TMP_DIR/movement_probe"

"$VIPER_BIN" build "$ROOT_DIR/examples/games/crackman/movement_probe.zia" -o "$IL_FILE"
"$VIPER_BIN" codegen arm64 "$IL_FILE" --native-asm --native-link -O2 -o "$BIN_FILE"

OUTPUT="$("$BIN_FILE")"
printf '%s\n' "$OUTPUT"

grep -q '^RESULT: ok$' <<<"$OUTPUT"

echo "PASS"
