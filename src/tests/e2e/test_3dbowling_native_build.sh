#!/bin/bash
set -euo pipefail

VIPER_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/3dbowling_native_build.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/3dbowling.il"
BIN_FILE="$TMP_DIR/3dbowling"

"$VIPER_BIN" build "$ROOT_DIR/examples/games/3dbowling" -o "$IL_FILE"
"$VIPER_BIN" codegen arm64 "$IL_FILE" --native-asm --native-link -O1 -o "$BIN_FILE"

test -f "$BIN_FILE"
test -x "$BIN_FILE"
echo "PASS"
