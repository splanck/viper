#!/bin/bash
set -euo pipefail

ZANNA_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/physics3d_fixed_step_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

PROBE="$ROOT_DIR/src/tests/e2e/physics3d_fixed_step_probe.zia"
IL_FILE="$TMP_DIR/physics3d_fixed_step.il"
BIN_FILE="$TMP_DIR/physics3d_fixed_step"

VM_OUTPUT="$("$ZANNA_BIN" run "$PROBE")"
"$ZANNA_BIN" build "$PROBE" -o "$IL_FILE"
"$ZANNA_BIN" codegen arm64 "$IL_FILE" --native-asm --native-link -O1 -o "$BIN_FILE"
NATIVE_OUTPUT="$("$BIN_FILE")"

if [ "$VM_OUTPUT" != "$NATIVE_OUTPUT" ]; then
    printf 'VM output:\n%s\n' "$VM_OUTPUT" >&2
    printf 'Native output:\n%s\n' "$NATIVE_OUTPUT" >&2
    exit 1
fi

printf '%s\n' "$VM_OUTPUT"
grep -q '^RESULT: ok$' <<<"$VM_OUTPUT"
echo "PASS"
