#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/src/tools/viper/viper}"
BASIC="src/tests/basic/mixed_labels.bas"
OUT="$("$BIN" front basic -run "$BASIC")"
EXPECTED=$'start\nlanded on 100'
if [[ "$OUT" == "$EXPECTED" ]]; then
  echo "PASS: GOTO jumps to labeled line"
  exit 0
else
  echo "FAIL: expected output:"
  printf '%s\n' "start" "landed on 100"
  echo "Actual output:"
  printf '%s\n' "$OUT"
  exit 1
fi
