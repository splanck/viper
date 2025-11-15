#!/usr/bin/env bash
# Smoke test to ensure BASIC array programs compile and run end-to-end.
set -euo pipefail
BIN="${1:-./build/src/tools/ilc/ilc}"

# Just assert they run without a compile-time error.
"$BIN" front basic -run tests/basic/array_float_narrowing.bas >/dev/null
"$BIN" front basic -run tests/basic/bubble_sort.bas >/dev/null
echo "PASS: array_float_narrowing & bubble_sort compile and run"
