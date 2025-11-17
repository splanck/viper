#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/src/tools/ilc/ilc}"

run() {
  local file="$1" expect="$2"
  if ! out="$("$BIN" front basic -run "$file")"; then
    echo "FAIL: $file crashed"
    echo "$out"
    exit 1
  fi
  if grep -q "$expect" <<<"$out"; then
    echo "PASS: $file -> $expect"
  else
    echo "FAIL: $file expected '$expect' but got:"
    echo "$out"
    exit 1
  fi
}

run src/tests/basic/while_unnumbered.bas "^3$"
run src/tests/basic/sum_two_ways.bas "WHILE=55 FOR=55"
