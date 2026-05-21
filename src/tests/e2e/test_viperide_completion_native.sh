#!/bin/bash
set -euo pipefail

VIPER_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viperide_completion_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/viperide_completion_probe.il"
BIN_FILE="$TMP_DIR/viperide_completion_probe"

"$VIPER_BIN" build "$ROOT_DIR/src/tests/e2e/viperide_completion_native_probe.zia" -o "$IL_FILE"
"$VIPER_BIN" codegen arm64 "$IL_FILE" --native-asm --native-link -O1 -o "$BIN_FILE"

OUTPUT="$("$BIN_FILE")"
printf '%s\n' "$OUTPUT"

grep -q '^RESULT: ok$' <<<"$OUTPUT"
echo "PASS"
