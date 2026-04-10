#!/bin/bash
set -euo pipefail

VIPER_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/xeno_native_start.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/start_probe.il"
BIN_FILE="$TMP_DIR/start_probe"

"$VIPER_BIN" build "$ROOT_DIR/examples/games/xenoscape/start_probe.zia" -o "$IL_FILE"
"$VIPER_BIN" codegen arm64 "$IL_FILE" --native-asm --native-link -O1 -o "$BIN_FILE"

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

echo "PASS"
