#!/usr/bin/env bash
set -euo pipefail

# VM benchmarking script for Viper
# - Benchmarks stress test IL programs across VM dispatch modes
# - Prints comparison table showing relative performance
# - Requires a built `ilc` executable

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"

# Configuration (override via env)
RUNS_PER_CASE="${RUNS_PER_CASE:-5}"
WARMUP_RUNS="${WARMUP_RUNS:-1}"
ILC_BIN="${ILC_BIN:-}"
IL_DIR="${IL_DIR:-examples/il/benchmarks}"
QUIET="${QUIET:-0}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

log() {
  [[ "$QUIET" == "1" ]] && return
  echo -e "$@"
}

log_progress() {
  [[ "$QUIET" == "1" ]] && return
  echo -ne "$@"
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
  )
  for c in "${candidates[@]}"; do
    if [[ -x "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

parse_time_ms() {
  echo "$1" | sed -n 's/.*time_ms=\([0-9.][0-9.]*\).*/\1/p' | tail -n1
}

parse_instr() {
  echo "$1" | sed -n 's/.*instr=\([0-9][0-9]*\).*/\1/p' | tail -n1
}

# Run benchmark for one file with one dispatch mode
# Returns: avg_ms
bench_one() {
  local ilc="$1" mode="$2" il_file="$3" runs="$4"

  export VIPER_ENABLE_OPCOUNTS=1
  export VIPER_DEBUG_VM=1
  export VIPER_DISPATCH="$mode"

  local times=""
  local count=0

  # Warmup
  for ((w=1; w<=WARMUP_RUNS; w++)); do
    "$ilc" -run "$il_file" --count --time >/dev/null 2>&1 || true
  done

  # Timed runs
  for ((i=1; i<=runs; i++)); do
    local out
    out=$("$ilc" -run "$il_file" --count --time 2>&1) || true

    local t
    t=$(parse_time_ms "$out")
    if [[ -n "$t" ]]; then
      times="$times $t"
      count=$((count + 1))
    fi
  done

  if [[ "$count" -eq 0 ]]; then
    echo "error"
    return
  fi

  # Calculate average
  local avg
  avg=$(echo "$times" | awk '{sum=0; for(i=1;i<=NF;i++) sum+=$i; print sum/NF}')
  printf "%.1f" "$avg"
}

print_header() {
  log ""
  log "${BOLD}═══════════════════════════════════════════════════════════════════════════════${NC}"
  log "${BOLD}                          VIPER VM DISPATCH BENCHMARK${NC}"
  log "${BOLD}═══════════════════════════════════════════════════════════════════════════════${NC}"
  log ""
  log "  Host:     $(uname -sm)"
  log "  Commit:   $(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  log "  ILC:      $1"
  log "  Runs:     $RUNS_PER_CASE (+ $WARMUP_RUNS warmup)"
  log ""
}

print_separator() {
  log "───────────────────────────────────────────────────────────────────────────────"
}

format_comparison() {
  local value="$1" base="$2"
  if [[ "$value" == "error" || "$base" == "error" ]]; then
    echo "error"
    return
  fi

  local pct
  pct=$(awk -v v="$value" -v b="$base" 'BEGIN{
    if (b == 0) print "0.0"
    else printf("%.1f", ((v - b) / b) * 100)
  }')

  if awk -v p="$pct" 'BEGIN{ exit !(p < -2) }'; then
    printf "${GREEN}%+.1f%%${NC}" "$pct"
  elif awk -v p="$pct" 'BEGIN{ exit !(p > 2) }'; then
    printf "${RED}%+.1f%%${NC}" "$pct"
  else
    printf "%.1f%%" "$pct"
  fi
}

main() {
  local ilc
  if ! ilc="$(find_ilc)"; then
    echo "error: could not locate 'ilc' executable. Build project first." >&2
    exit 1
  fi

  # Find benchmark IL files
  local il_files=()
  if [[ -d "$ROOT_DIR/$IL_DIR" ]]; then
    while IFS= read -r f; do
      [[ -n "$f" ]] && il_files+=("$f")
    done < <(find "$ROOT_DIR/$IL_DIR" -name "*.il" -type f | sort)
  fi

  if [[ "${#il_files[@]}" -eq 0 ]]; then
    echo "error: no IL files found in $IL_DIR" >&2
    exit 1
  fi

  print_header "$ilc"

  local modes=(table switch threaded)
  local total_tests=$((${#il_files[@]} * ${#modes[@]}))
  local current=0

  # Temporary files to store results
  local tmp_results=$(mktemp)
  trap "rm -f $tmp_results" EXIT

  # Run all benchmarks and store results
  for il in "${il_files[@]}"; do
    local basename
    basename=$(basename "$il" .il)

    for mode in "${modes[@]}"; do
      ((current++)) || true
      log_progress "\r  Running benchmark $current/$total_tests: $basename ($mode)...              "

      local result
      result=$(bench_one "$ilc" "$mode" "$il" "$RUNS_PER_CASE")
      echo "$basename $mode $result" >> "$tmp_results"
    done
  done

  log_progress "\r                                                                                    \r"

  # Print results table
  print_separator
  log ""
  log "${BOLD}  RESULTS (avg time in ms, lower is better)${NC}"
  log ""
  printf "  ${BOLD}%-18s %12s %12s %12s %14s %14s${NC}\n" "Benchmark" "Table" "Switch" "Threaded" "Sw vs Tbl" "Th vs Tbl"
  print_separator

  local table_total=0 switch_total=0 threaded_total=0

  for il in "${il_files[@]}"; do
    local basename
    basename=$(basename "$il" .il)

    local table_ms switch_ms threaded_ms
    table_ms=$(grep "^$basename table " "$tmp_results" | awk '{print $3}')
    switch_ms=$(grep "^$basename switch " "$tmp_results" | awk '{print $3}')
    threaded_ms=$(grep "^$basename threaded " "$tmp_results" | awk '{print $3}')

    local sw_cmp th_cmp
    sw_cmp=$(format_comparison "$switch_ms" "$table_ms")
    th_cmp=$(format_comparison "$threaded_ms" "$table_ms")

    printf "  %-18s %12s %12s %12s %14b %14b\n" \
      "$basename" "${table_ms}ms" "${switch_ms}ms" "${threaded_ms}ms" "$sw_cmp" "$th_cmp"

    # Accumulate totals
    if [[ "$table_ms" != "error" ]]; then
      table_total=$(awk -v a="$table_total" -v b="$table_ms" 'BEGIN{print a+b}')
    fi
    if [[ "$switch_ms" != "error" ]]; then
      switch_total=$(awk -v a="$switch_total" -v b="$switch_ms" 'BEGIN{print a+b}')
    fi
    if [[ "$threaded_ms" != "error" ]]; then
      threaded_total=$(awk -v a="$threaded_total" -v b="$threaded_ms" 'BEGIN{print a+b}')
    fi
  done

  print_separator
  log ""

  # Summary
  log "${BOLD}  SUMMARY${NC}"
  log ""

  local switch_vs_table threaded_vs_table
  switch_vs_table=$(awk -v s="$switch_total" -v t="$table_total" 'BEGIN{ printf("%.1f", ((s - t) / t) * 100) }')
  threaded_vs_table=$(awk -v th="$threaded_total" -v t="$table_total" 'BEGIN{ printf("%.1f", ((th - t) / t) * 100) }')

  printf "  Total (Table):    %10.1f ms\n" "$table_total"
  printf "  Total (Switch):   %10.1f ms (%+.1f%% vs Table)\n" "$switch_total" "$switch_vs_table"
  printf "  Total (Threaded): %10.1f ms (%+.1f%% vs Table)\n" "$threaded_total" "$threaded_vs_table"
  log ""

  # Determine winner
  local winner="Table"
  local best="$table_total"
  if awk -v s="$switch_total" -v b="$best" 'BEGIN{ exit !(s < b) }'; then
    winner="Switch"
    best="$switch_total"
  fi
  if awk -v t="$threaded_total" -v b="$best" 'BEGIN{ exit !(t < b) }'; then
    winner="Threaded"
  fi

  log "  ${GREEN}${BOLD}Winner: $winner${NC}"
  log ""
  log "${BOLD}═══════════════════════════════════════════════════════════════════════════════${NC}"
}

main "$@"
