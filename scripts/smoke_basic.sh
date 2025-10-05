#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="${ROOT_DIR}/build"
ILC_BIN="${BUILD_DIR}/src/tools/ilc/ilc"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

cases=(
  "tests/smoke/basic/unnumbered_two_lines.bas"
  "tests/smoke/basic/arrays_print.bas"
  "tests/smoke/basic/do_while_no_arrays.bas"
  "tests/smoke/basic/for_latch_pos.bas"
  "tests/smoke/basic/for_latch_neg.bas"
  "tests/smoke/basic/short_circuit_guard.bas"
)

expected_substrings=(
  "2"
  "7"
  "1"
  "1"
  "1"
  "1"
)

for i in "${!cases[@]}"; do
  rel_path="${cases[i]}"
  expected="${expected_substrings[i]}"
  abs_path="${ROOT_DIR}/${rel_path}"
  echo "Running ${rel_path}..."
  if ! output="$(timeout 10s "${ILC_BIN}" front basic -run "${abs_path}")"; then
    echo "ERROR: ${rel_path} exited with failure" >&2
    exit 1
  fi
  if ! grep -q "${expected}" <<<"${output}"; then
    echo "ERROR: ${rel_path} output missing substring '${expected}'" >&2
    echo "Output was:" >&2
    echo "${output}" >&2
    exit 1
  fi
  echo "${rel_path} passed"
  echo
done

echo "All smoke tests passed."
