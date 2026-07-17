#!/usr/bin/env bash
# File: scripts/test_strings.sh
# Purpose: Run BASIC string intrinsic tests and compare outputs with golden files.
# Key invariants: For each tests/basic/strings/*.bas file there is a matching .golden.
# Usage: scripts/test_strings.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ZANNA_BUILD_DIR:-$ROOT/build}}"
ILC="$BUILD_DIR/src/tools/zanna/zanna"
FAIL=0
for BAS in "$ROOT"/src/tests/basic/strings/*.bas; do
  NAME="$(basename "$BAS" .bas)"
  GOLDEN="$ROOT/src/tests/basic/strings/$NAME.golden"
  OUT="$(mktemp)"
  if ! "$ILC" front basic -run "$BAS" > "$OUT"; then
    echo "[test_strings] FAIL $NAME (run)" >&2
    FAIL=1
  elif ! diff -u "$GOLDEN" "$OUT"; then
    echo "[test_strings] FAIL $NAME (diff)" >&2
    FAIL=1
  else
    echo "[test_strings] PASS $NAME"
  fi
  rm -f "$OUT"
done
exit $FAIL
