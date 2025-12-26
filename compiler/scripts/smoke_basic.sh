#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="${ROOT_DIR}/build"
ILC_BIN="${BUILD_DIR}/src/tools/ilc/ilc"

if command -v timeout >/dev/null 2>&1; then
  run_with_timeout() {
    timeout "$@"
  }
else
  run_with_timeout() {
    local _duration="$1"
    shift
    "$@"
  }
fi

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
  if ! output="$(run_with_timeout 10s "${ILC_BIN}" front basic -run "${abs_path}")"; then
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

concat_string_path="${ROOT_DIR}/tests/smoke/basic/concat_string.bas"
echo "Running tests/smoke/basic/concat_string.bas..."
if ! output="$(run_with_timeout 10s "${ILC_BIN}" front basic -run "${concat_string_path}")"; then
  echo "ERROR: tests/smoke/basic/concat_string.bas exited with failure" >&2
  exit 1
fi
if ! grep -Fq "Hello, World" <<<"${output}"; then
  echo "ERROR: tests/smoke/basic/concat_string.bas output missing substring 'Hello, World'" >&2
  echo "Output was:" >&2
  echo "${output}" >&2
  exit 1
fi
echo "tests/smoke/basic/concat_string.bas passed"
echo

concat_input_path="${ROOT_DIR}/tests/smoke/basic/concat_input.bas"
echo "Running tests/smoke/basic/concat_input.bas..."
if ! output="$(printf "Alice\n" | run_with_timeout 10s "${ILC_BIN}" front basic -run "${concat_input_path}")"; then
  echo "ERROR: tests/smoke/basic/concat_input.bas exited with failure" >&2
  exit 1
fi
if ! grep -Fq "Alice!" <<<"${output}"; then
  echo "ERROR: tests/smoke/basic/concat_input.bas output missing substring 'Alice!'" >&2
  echo "Output was:" >&2
  echo "${output}" >&2
  exit 1
fi
echo "tests/smoke/basic/concat_input.bas passed"
echo

echo "All smoke tests passed."

# String concat test
run_with_timeout 2 "$BUILD_ROOT/src/tools/ilc/ilc" front basic -run tests/smoke/basic/strings_concat.bas | grep -F "Hello, World"

# String aliasing test
printf "Stephen\n" | run_with_timeout 2 "$BUILD_ROOT/src/tools/ilc/ilc" front basic -run tests/smoke/basic/strings_alias.bas | grep -F "StephenStephen"
