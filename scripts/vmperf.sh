#!/usr/bin/env bash

set -euo pipefail

# vmperf.sh — Run VM performance benchmarks across switch backends
#
# Usage:
#   scripts/vmperf.sh [BUILD_DIR] [ITERATIONS]
#
# Defaults:
#   BUILD_DIR   = ./build
#   ITERATIONS  = 3
#
# The script runs perf_vm_switch_bench and perf_vm_dispatch_bench under each
# switch backend (dense, sorted, hashed) by setting VIPER_SWITCH_MODE.
# It reports the median real time per (mode, benchmark) pair.

BUILD_DIR="${1:-build}"
ITERATIONS="${2:-3}"

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "error: build dir '$BUILD_DIR' not found" >&2
  exit 1
fi

# Resolve benchmark executables. Try common paths first, then search.
resolve_bin_one() {
  local name="$1"
  local cand1="$BUILD_DIR/src/tests/$name"
  local cand2="$BUILD_DIR/$name"
  if [[ -x "$cand1" ]]; then echo "$cand1"; return; fi
  if [[ -x "$cand2" ]]; then echo "$cand2"; return; fi
  local found
  found=$(find "$BUILD_DIR" -type f -name "$name" -perm -111 2>/dev/null | head -n 1 || true)
  if [[ -n "$found" ]]; then echo "$found"; return; fi
  echo ""
}

resolve_bin() {
  local path=""
  for candidate in "$@"; do
    path=$(resolve_bin_one "$candidate")
    if [[ -n "$path" ]]; then echo "$path"; return; fi
  done
  if command -v ctest >/dev/null 2>&1; then
    local name cmd
    for name in "$@"; do
      cmd=$(ctest --test-dir "$BUILD_DIR" -V -R "^${name}$" 2>/dev/null | sed -n 's/^    Command: \(.*\)$/\1/p' | head -n1 || true)
      if [[ -n "$cmd" ]]; then
        echo "$cmd" | awk '{print $1}'
        return
      fi
    done
  fi
  echo ""
}

SWITCH_BENCH="$(resolve_bin perf_vm_switch_bench test_perf_vm_switch)"
DISPATCH_BENCH="$(resolve_bin perf_vm_dispatch_bench test_perf_vm_dispatch)"

if [[ -z "$SWITCH_BENCH" || ! -x "$SWITCH_BENCH" ]]; then
  echo "error: could not locate 'perf_vm_switch_bench' (aka test_perf_vm_switch) in $BUILD_DIR" >&2
  exit 1
fi
if [[ -z "$DISPATCH_BENCH" || ! -x "$DISPATCH_BENCH" ]]; then
  echo "error: could not locate 'perf_vm_dispatch_bench' (aka test_perf_vm_dispatch) in $BUILD_DIR" >&2
  exit 1
fi

TIME_BIN=""
if command -v gtime >/dev/null 2>&1; then
  TIME_BIN="gtime -p"
elif command -v /usr/bin/time >/dev/null 2>&1; then
  TIME_BIN="/usr/bin/time -p"
else
  echo "warning: 'time' not found; results may be less precise" >&2
fi

run_timed() {
  local mode="$1"; shift
  local exe="$1"; shift
  local i
  local times=()
  for (( i=1; i<=ITERATIONS; ++i )); do
    if [[ -n "$TIME_BIN" ]]; then
      # Capture 'real' seconds from time -p
      local out
      out=$( ( VIPER_SWITCH_MODE="$mode" $TIME_BIN "$exe" ) 2>&1 >/dev/null | awk '/^real /{print $2}')
      times+=("$out")
      echo "  iter $i: ${out}s"
    else
      local start end dur
      start=$(date +%s.%N)
      VIPER_SWITCH_MODE="$mode" "$exe" >/dev/null
      end=$(date +%s.%N)
      dur=$(awk -v s="$start" -v e="$end" 'BEGIN{printf "%.3f", (e-s)}')
      times+=("$dur")
      echo "  iter $i: ${dur}s"
    fi
  done
  # sort numeric and pick median
  IFS=$'\n' sorted=($(printf '%s\n' "${times[@]}" | LC_ALL=C sort -n))
  unset IFS
  local n=${#sorted[@]}
  local mid=$(( (n-1)/2 ))
  echo "${sorted[$mid]}"
}

report_row() {
  local mode="$1"
  echo "\n== Mode: $mode =="
  echo "Switch bench:"
  local s_med
  s_med=$(run_timed "$mode" "$SWITCH_BENCH")
  echo "  median: ${s_med}s"
  echo "Dispatch bench:"
  local d_med
  d_med=$(run_timed "$mode" "$DISPATCH_BENCH")
  echo "  median: ${d_med}s"
}

echo "Using build dir: $BUILD_DIR"
echo "Benchmarks:"
echo "  switch:   $SWITCH_BENCH"
echo "  dispatch: $DISPATCH_BENCH"
echo "Iterations: $ITERATIONS"

for MODE in dense sorted hashed; do
  report_row "$MODE"
done

echo "\nTip: set ITERATIONS higher for more stable results, e.g. 'scripts/vmperf.sh build 5'"
