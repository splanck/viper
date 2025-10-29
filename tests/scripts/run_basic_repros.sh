#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/src/tools/ilc/ilc}"

run() {
  local file="$1" expect="$2"
  out="$("$BIN" front basic -run "$file")" || {
    echo "FAIL: $file crashed"
    echo "$out"
    exit 1
  }
  if grep -q "$expect" <<<"$out"; then
    echo "PASS: $file -> $expect"
  else
    echo "FAIL: $file expected '$expect' but got:"
    echo "$out"
    exit 1
  }
}

run tests/basic/while_unnumbered.bas "^3$"
run tests/basic/sum_two_ways.bas "WHILE=55 FOR=55"
