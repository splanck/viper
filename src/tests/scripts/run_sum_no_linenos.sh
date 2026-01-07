#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/src/tools/ilc/ilc}"
BASIC="src/tests/basic/sum_no_linenos.bas"
OUT="$("$BIN" front basic -run "$BASIC")"
# Expect exact output:
# WHILE=55 FOR=55
if grep -q "WHILE=55 FOR=55" <<<"$OUT"; then
  echo "PASS: unlabeled BASIC lowers and runs"
  exit 0
else
  echo "FAIL: expected 'WHILE=55 FOR=55' but got:"
  echo "$OUT"
  exit 1
fi
