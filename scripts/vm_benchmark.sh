#!/usr/bin/env bash
set -euo pipefail

# VM benchmarking script for Viper
# - Benchmarks a suite of IL programs across VM dispatch modes
# - Requires a built `ilc` executable
# - Appends results to /bugs/vm_benchmarks.md

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUGS_MD="$ROOT_DIR/bugs/vm_benchmarks.md"

# Configuration (override via env)
RUNS_PER_CASE="${RUNS_PER_CASE:-3}"
WARMUP_PER_CASE="${WARMUP_PER_CASE:-0}"
ILC_BIN="${ILC_BIN:-}"
IL_GLOB="${IL_GLOB:-examples/il/*.il}"

timestamp() {
  date '+%Y-%m-%d %H:%M:%S %z'
}

find_ilc() {
  if [[ -n "${ILC_BIN}" && -x "${ILC_BIN}" ]]; then
    echo "${ILC_BIN}"
    return 0
  fi
  local candidates=(
    "$ROOT_DIR/build/src/tools/ilc/ilc"
    "$ROOT_DIR/build/tools/ilc/ilc"
    "$ROOT_DIR/build/bin/ilc"
    "$ROOT_DIR/ilc"
    "$(command -v ilc 2>/dev/null || true)"
  )
  for c in "${candidates[@]}"; do
    if [[ -n "$c" && -x "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "error: missing file: $path" >&2
    exit 1
  fi
}

ensure_paths() {
  mkdir -p "$ROOT_DIR/bugs" "$ROOT_DIR/scripts"
}

# Parse summary from ilc stderr output
# echoes: time_ms instr
parse_summary() {
  local text="$1"
  local time_ms instr
  time_ms=$(echo "$text" | sed -n 's/.*time_ms=\([0-9.][0-9.]*\).*/\1/p' | tail -n1)
  instr=$(echo "$text" | sed -n 's/.*instr=\([0-9][0-9]*\).*/\1/p' | tail -n1)
  echo "${time_ms:-} ${instr:-}"
}

# Extract actual dispatch kind from debug line
# echoes: DispatchKind or empty
parse_dispatch_kind() {
  local text="$1"
  echo "$text" | sed -n 's/.*\[DEBUG\]\[VM\] dispatch kind: \([A-Za-z][A-Za-z]*\).*/\1/p' | head -n1
}

bench_one_case() {
  local ilc="$1" mode="$2" il_file="$3" runs="$4"

  local times=()
  local instr_first=""
  local dispatch_actual=""

  # Enable opcode counting and debug dispatch line
  export VIPER_ENABLE_OPCOUNTS=1
  export VIPER_DEBUG_VM=1
  export VIPER_DISPATCH="$mode"

  # Warm-up runs (not recorded)
  if [[ "$WARMUP_PER_CASE" -gt 0 ]]; then
    for ((w=1; w<=WARMUP_PER_CASE; w++)); do
      "$ilc" -run "$il_file" --count --time 2>&1 >/dev/null || true
    done
  fi

  for ((i=1; i<=runs; i++)); do
    # Capture only stderr from ilc (stdout may contain program output)
    local out
    if ! out=$("$ilc" -run "$il_file" --count --time 2>&1 >/dev/null); then
      # Even on non-zero exit, capture any summary/debug emitted so far
      :
    fi
    # Detect actual dispatch (first run sufficient)
    if [[ -z "$dispatch_actual" ]]; then
      dispatch_actual="$(parse_dispatch_kind "$out")"
    fi
    # Parse metrics
    read -r t instr <<<"$(parse_summary "$out")"
    if [[ -z "${t:-}" ]]; then
      # If timing not present, skip recording
      continue
    fi
    times+=("$t")
    if [[ -z "$instr_first" && -n "${instr:-}" ]]; then
      instr_first="$instr"
    fi
  done

  # Aggregate
  local count="${#times[@]}"
  if [[ "$count" -eq 0 ]]; then
    echo ";error;no-data;no-data;no-data;no-data"  # marker for caller
    return 0
  fi
  local sum=0 min= max=
  for t in "${times[@]}"; do
    # Use awk for floating add
    sum=$(awk -v a="$sum" -v b="$t" 'BEGIN{ printf("%.6f", a + b) }')
    if [[ -z "$min" ]] || awk -v a="$t" -v b="$min" 'BEGIN{ exit !(a < b) }'; then
      min="$t"
    fi
    if [[ -z "$max" ]] || awk -v a="$t" -v b="$max" 'BEGIN{ exit !(a > b) }'; then
      max="$t"
    fi
  done
  local avg
  avg=$(awk -v s="$sum" -v n="$count" 'BEGIN{ printf("%.6f", (n>0)? s/n : 0) }')
  echo ";${dispatch_actual:-unknown};${count};${avg};${min};${max};${instr_first:-}"
}

main() {
  ensure_paths
  local ilc
  if ! ilc="$(find_ilc)"; then
    echo "error: could not locate 'ilc' executable. Build the project or set ILC_BIN." >&2
    exit 1
  fi

  # Resolve IL files
  shopt -s nullglob
  local il_files=()
  while IFS= read -r -d '' f; do il_files+=("$f"); done < <(printf '%s\0' $ROOT_DIR/$IL_GLOB)
  shopt -u nullglob
  if [[ "${#il_files[@]}" -eq 0 ]]; then
    echo "error: no IL files matched glob: $IL_GLOB" >&2
    exit 1
  fi

  local host="$(uname -sm)"
  local commit="$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  local ts="$(timestamp)"

  {
    echo ""
    echo "## ${ts} — VM Benchmarks"
    echo "- Host: ${host}"
    echo "- Commit: ${commit}"
    echo "- Runs per case: ${RUNS_PER_CASE}"
    echo "- IL glob: ${IL_GLOB}"
    echo "- ILC: ${ilc}"
    echo ""
    echo "| timestamp | file | requested | actual | runs | avg_ms | min_ms | max_ms | instr |"
    echo "|---|---|---:|---:|---:|---:|---:|---:|---:|"
  } >>"$BUGS_MD"

  local modes=(table switch threaded)
  for mode in "${modes[@]}"; do
    for il in "${il_files[@]}"; do
      # Run benchmark
      local suffix
      suffix="$(bench_one_case "$ilc" "$mode" "$il" "$RUNS_PER_CASE")"

      local req="$mode"
      local actual runs avg min max instr
      IFS=';' read -r _ actual runs avg min max instr <<<"$suffix"

      if [[ "$actual" == "error" ]]; then
        actual="error"; runs="0"; avg="-"; min="-"; max="-"; instr="-"
      fi

      # Timestamp per benchmark row
      row_ts="$(timestamp)"
      # Write a row
      printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n" \
        "$row_ts" "${il#$ROOT_DIR/}" "$req" "$actual" "$runs" "$avg" "$min" "$max" "$instr" \
        >>"$BUGS_MD"
    done
  done
}

main "$@"
