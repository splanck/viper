#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# Viper Unified Benchmark Suite
# ============================================================================
# Benchmarks all execution modes across IL stress programs and compares
# against C (-O0/-O2/-O3), Rust, Lua, Python, and Java reference implementations.
#
# Usage: scripts/benchmark.sh [options]
#
# Modes:
#   vm-table, vm-switch, vm-threaded    Standard VM dispatch strategies
#   bc-switch, bc-threaded              Bytecode VM dispatch strategies
#   native-arm64, native-x86_64        Native codegen backends
#   c-O0, c-O2, c-O3                   C reference at optimization levels
#   rust-O                              Rust reference (rustc -O)
#   lua                                 Lua reference (lua 5.4)
#   python3                             Python 3 reference
#   java                                Java reference
# ============================================================================

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"

# --- Defaults ---
ITERATIONS=5
WARMUP=1
SET_BASELINE=0
NO_NATIVE=0
NO_VM=0
NO_REFERENCE=0
PROGRAMS_GLOB=""
QUIET=0

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# --- Temp files ---
TMPDIR_BENCH=$(mktemp -d)
trap "rm -rf '$TMPDIR_BENCH'" EXIT

# ============================================================================
# Argument parsing
# ============================================================================
usage() {
    cat <<'EOF'
Usage: scripts/benchmark.sh [options]

Options:
  -n, --iterations N     Timed iterations per benchmark (default: 5)
  --warmup N             Warmup iterations (default: 1)
  --set-baseline         Save this run as benchmarks/baseline.jsonl
  --no-native            Skip native codegen benchmarks
  --no-vm                Skip VM benchmarks
  --no-reference         Skip C/Rust/Lua/Python/Java reference benchmarks
  --programs GLOB        Only run programs matching this glob (e.g. "fib*")
  -q, --quiet            Suppress progress output
  -h, --help             Show this help message
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n|--iterations) ITERATIONS="$2"; shift 2 ;;
        --warmup) WARMUP="$2"; shift 2 ;;
        --set-baseline) SET_BASELINE=1; shift ;;
        --no-native) NO_NATIVE=1; shift ;;
        --no-vm) NO_VM=1; shift ;;
        --no-reference) NO_REFERENCE=1; shift ;;
        --programs) PROGRAMS_GLOB="$2"; shift 2 ;;
        -q|--quiet) QUIET=1; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; usage ;;
    esac
done

# ============================================================================
# Helpers
# ============================================================================
log() {
    [[ "$QUIET" == "1" ]] && return
    echo -e "$@"
}

log_progress() {
    [[ "$QUIET" == "1" ]] && return
    echo -ne "$@" >&2
}

# ============================================================================
# Platform detection
# ============================================================================
ARCH=$(uname -m)
OS=$(uname -s)

HAS_NATIVE_ARM64=0
HAS_NATIVE_X86_64=0
HAS_CC=0
HAS_PYTHON3=0
HAS_JAVA=0
HAS_LUA=0
HAS_RUSTC=0

if [[ "$ARCH" == "arm64" || "$ARCH" == "aarch64" ]]; then
    HAS_NATIVE_ARM64=1
fi
if [[ "$ARCH" == "x86_64" ]]; then
    HAS_NATIVE_X86_64=1
fi
if command -v cc >/dev/null 2>&1; then
    HAS_CC=1
fi
if command -v python3 >/dev/null 2>&1; then
    HAS_PYTHON3=1
fi
if command -v lua >/dev/null 2>&1; then
    HAS_LUA=1
fi
# Rust: check both PATH and cargo env
RUSTC="rustc"
if command -v rustc >/dev/null 2>&1; then
    HAS_RUSTC=1
elif [[ -f "$HOME/.cargo/env" ]]; then
    # shellcheck disable=SC1091
    . "$HOME/.cargo/env"
    if command -v rustc >/dev/null 2>&1; then
        HAS_RUSTC=1
        RUSTC="rustc"
    fi
fi
JAVAC="javac"
JAVA="java"
if javac -version >/dev/null 2>&1 && java -version >/dev/null 2>&1; then
    HAS_JAVA=1
elif [[ -x /opt/homebrew/opt/openjdk/bin/javac ]] && /opt/homebrew/opt/openjdk/bin/java -version >/dev/null 2>&1; then
    HAS_JAVA=1
    JAVAC=/opt/homebrew/opt/openjdk/bin/javac
    JAVA=/opt/homebrew/opt/openjdk/bin/java
fi

# python3 is required for JSON assembly and timing
if [[ "$HAS_PYTHON3" != "1" ]]; then
    echo "error: python3 is required for JSON assembly and timing" >&2
    exit 1
fi

# ============================================================================
# Find viper binary
# ============================================================================
find_viper() {
    local candidates=(
        "$ROOT_DIR/build/src/tools/viper/viper"
        "$ROOT_DIR/build/tools/viper/viper"
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

VIPER=""
if ! VIPER="$(find_viper)"; then
    echo "error: could not locate viper binary. Build first: cmake -S . -B build && cmake --build build -j" >&2
    exit 1
fi

# ============================================================================
# Metadata collection
# ============================================================================
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%S")
COMMIT=$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")
BRANCH=$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
PLATFORM="$OS $ARCH"

# CPU model
get_cpu_model() {
    if [[ "$OS" == "Darwin" ]]; then
        sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "unknown"
    elif [[ -f /proc/cpuinfo ]]; then
        grep -m1 "model name" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs || echo "unknown"
    else
        echo "unknown"
    fi
}
CPU_MODEL=$(get_cpu_model)

# ============================================================================
# Timing helpers
# ============================================================================

# Time an executable N times, return JSON: {"time_ms":M,"return_value":R,"success":true}
# Args: exe iterations warmup [extra_args...]
time_executable() {
    local exe="$1" iters="$2" warm="$3"
    shift 3
    local extra_args=()
    if [[ $# -gt 0 ]]; then extra_args=("$@"); fi

    # Warmup
    for ((w=0; w<warm; w++)); do
        "$exe" ${extra_args[@]+"${extra_args[@]}"} >/dev/null 2>&1 || true
    done

    # Timed runs
    local times_file="$TMPDIR_BENCH/times_$$_$RANDOM"
    local last_rc=0
    for ((i=0; i<iters; i++)); do
        local t0 t1
        t0=$(python3 -c 'import time;print(time.monotonic())')
        "$exe" ${extra_args[@]+"${extra_args[@]}"} >/dev/null 2>&1
        last_rc=$?
        t1=$(python3 -c 'import time;print(time.monotonic())')
        python3 -c "print(round(($t1-$t0)*1000,3))" >> "$times_file"
    done

    # Compute median
    local median
    median=$(sort -n "$times_file" | awk '{a[NR]=$1} END{print a[int(NR/2)+1]}')
    local rv=$((last_rc % 256))

    rm -f "$times_file"
    echo "{\"time_ms\":$median,\"return_value\":$rv,\"success\":true}"
}

# Time a python script
time_python() {
    local script="$1" iters="$2" warm="$3"

    # Warmup
    for ((w=0; w<warm; w++)); do
        python3 "$script" >/dev/null 2>&1 || true
    done

    local times_file="$TMPDIR_BENCH/times_py_$$_$RANDOM"
    local last_rc=0
    for ((i=0; i<iters; i++)); do
        local t0 t1
        t0=$(python3 -c 'import time;print(time.monotonic())')
        python3 "$script" >/dev/null 2>&1
        last_rc=$?
        t1=$(python3 -c 'import time;print(time.monotonic())')
        python3 -c "print(round(($t1-$t0)*1000,3))" >> "$times_file"
    done

    local median
    median=$(sort -n "$times_file" | awk '{a[NR]=$1} END{print a[int(NR/2)+1]}')
    local rv=$((last_rc % 256))

    rm -f "$times_file"
    echo "{\"time_ms\":$median,\"return_value\":$rv,\"success\":true}"
}

# Convert snake_case to PascalCase
snake_to_pascal() {
    echo "$1" | python3 -c "import sys; print(''.join(w.capitalize() for w in sys.stdin.read().strip().split('_')))"
}

# ============================================================================
# Check if IL file uses runtime externals
# ============================================================================
has_runtime_deps() {
    grep -q '^extern ' "$1" 2>/dev/null
}

# ============================================================================
# Enumerate benchmark programs
# ============================================================================
IL_DIR="$ROOT_DIR/examples/il/benchmarks"
REF_DIR="$ROOT_DIR/benchmarks/reference"

declare -a PROGRAMS=()
for f in "$IL_DIR"/*.il; do
    [[ -f "$f" ]] || continue
    local_name=$(basename "$f" .il)
    if [[ -n "$PROGRAMS_GLOB" ]]; then
        # shellcheck disable=SC2254
        case "$local_name" in
            $PROGRAMS_GLOB) ;;
            *) continue ;;
        esac
    fi
    PROGRAMS+=("$local_name")
done

if [[ ${#PROGRAMS[@]} -eq 0 ]]; then
    echo "error: no benchmark programs found in $IL_DIR" >&2
    exit 1
fi

# ============================================================================
# Print header
# ============================================================================
log ""
log "${BOLD}══════════════════════════════════════════════════════════════════════════${NC}"
log "${BOLD}                      VIPER UNIFIED BENCHMARK SUITE${NC}"
log "${BOLD}══════════════════════════════════════════════════════════════════════════${NC}"
log ""
log "  Platform:    $PLATFORM"
log "  CPU:         $CPU_MODEL"
log "  Commit:      $COMMIT ($BRANCH)"
log "  Viper:       $VIPER"
log "  Iterations:  $ITERATIONS (+ $WARMUP warmup)"
log "  Programs:    ${#PROGRAMS[*]}"
log ""
log "  Available modes:"
[[ "$NO_VM" != "1" ]] && log "    Bytecode:  bc-switch, bc-threaded"
[[ "$NO_NATIVE" != "1" && "$HAS_NATIVE_ARM64" == "1" ]] && log "    Native:    arm64"
[[ "$NO_NATIVE" != "1" && "$HAS_NATIVE_X86_64" == "1" ]] && log "    Native:    x86_64"
if [[ "$NO_REFERENCE" != "1" ]]; then
    [[ "$HAS_CC" == "1" ]] && log "    C:         -O0, -O2, -O3"
    [[ "$HAS_RUSTC" == "1" ]] && log "    Rust:      rustc -O"
    [[ "$HAS_LUA" == "1" ]] && log "    Lua:       lua"
    [[ "$HAS_PYTHON3" == "1" ]] && log "    Python:    python3"
    [[ "$HAS_JAVA" == "1" ]] && log "    Java:      javac + java"
fi
log ""
log "──────────────────────────────────────────────────────────────────────────"
log ""

# ============================================================================
# Run benchmarks
# ============================================================================

# Results accumulator: JSON fragments per program
declare -a PROGRAM_RESULTS=()

total_benchmarks=0
completed_benchmarks=0

# Count total work
for prog in "${PROGRAMS[@]}"; do
    il_file="$IL_DIR/$prog.il"
    if [[ "$NO_VM" != "1" ]]; then ((total_benchmarks += 2)) || true; fi
    if [[ "$NO_NATIVE" != "1" ]]; then
        [[ "$HAS_NATIVE_ARM64" == "1" ]] && ((total_benchmarks++)) || true
        [[ "$HAS_NATIVE_X86_64" == "1" ]] && ((total_benchmarks++)) || true
    fi
    if [[ "$NO_REFERENCE" != "1" ]]; then
        [[ "$HAS_CC" == "1" && -f "$REF_DIR/c/$prog.c" ]] && ((total_benchmarks += 3)) || true
        [[ "$HAS_RUSTC" == "1" && -f "$REF_DIR/rust/$prog.rs" ]] && ((total_benchmarks++)) || true
        [[ "$HAS_LUA" == "1" && -f "$REF_DIR/lua/$prog.lua" ]] && ((total_benchmarks++)) || true
        local_java_class=$(snake_to_pascal "$prog")
        [[ "$HAS_PYTHON3" == "1" && -f "$REF_DIR/python/$prog.py" ]] && ((total_benchmarks++)) || true
        [[ "$HAS_JAVA" == "1" && -f "$REF_DIR/java/$local_java_class.java" ]] && ((total_benchmarks++)) || true
    fi
done

for prog in "${PROGRAMS[@]}"; do
    il_file="$IL_DIR/$prog.il"
    modes_json=""

    # --- Bytecode VM modes ---
    if [[ "$NO_VM" != "1" ]]; then
        log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: bytecode VM...                  "

        vm_json=$("$VIPER" bench "$il_file" --bytecode --json -n "$ITERATIONS" 2>/dev/null || echo "[]")

        # Parse VM JSON — only bc-switch and bc-threaded
        vm_modes=$(python3 -c "
import json, sys
try:
    data = json.loads('''$vm_json''')
except:
    data = []
parts = []
for r in data:
    key = r.get('strategy','')
    if key not in ('bc-switch','bc-threaded'):
        continue
    obj = {'time_ms': r.get('time_ms',0), 'success': r.get('success',True), 'return_value': r.get('return_value',0)}
    if 'instructions' in r and r['instructions']:
        obj['instructions'] = r['instructions']
    if 'insns_per_sec' in r and r['insns_per_sec']:
        obj['insns_per_sec'] = r['insns_per_sec']
    parts.append('\"' + key + '\":' + json.dumps(obj))
print(','.join(parts))
" 2>/dev/null || echo "")

        if [[ -n "$vm_modes" ]]; then
            modes_json="$vm_modes"
        fi
        ((completed_benchmarks += 2)) || true
    fi

    # --- Native modes ---
    if [[ "$NO_NATIVE" != "1" ]]; then
        # Native ARM64
        if [[ "$HAS_NATIVE_ARM64" == "1" ]]; then
            log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: native-arm64...                "
            native_exe="$TMPDIR_BENCH/native_arm64_$prog"
            if "$VIPER" codegen arm64 "$il_file" -O2 -o "$native_exe" >/dev/null 2>&1; then
                result=$(time_executable "$native_exe" "$ITERATIONS" "$WARMUP")
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"native-arm64\":$result"
            else
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"native-arm64\":{\"success\":false,\"skip_reason\":\"compilation failed\"}"
            fi
            ((completed_benchmarks++)) || true
        fi

        # Native x86_64
        if [[ "$HAS_NATIVE_X86_64" == "1" ]]; then
            log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: native-x86_64...               "
            native_exe="$TMPDIR_BENCH/native_x64_$prog"
            if "$VIPER" codegen x64 "$il_file" -O2 -o "$native_exe" >/dev/null 2>&1; then
                result=$(time_executable "$native_exe" "$ITERATIONS" "$WARMUP")
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"native-x86_64\":$result"
            else
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"native-x86_64\":{\"success\":false,\"skip_reason\":\"compilation failed\"}"
            fi
            ((completed_benchmarks++)) || true
        fi
    fi

    # --- C reference ---
    if [[ "$NO_REFERENCE" != "1" && "$HAS_CC" == "1" && -f "$REF_DIR/c/$prog.c" ]]; then
        for opt in O0 O2 O3; do
            log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: c-$opt...                      "
            c_exe="$TMPDIR_BENCH/c_${opt}_$prog"
            if cc "-$opt" -o "$c_exe" "$REF_DIR/c/$prog.c" -lm 2>/dev/null; then
                result=$(time_executable "$c_exe" "$ITERATIONS" "$WARMUP")
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"c-$opt\":$result"
            else
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"c-$opt\":{\"success\":false,\"skip_reason\":\"compilation failed\"}"
            fi
            ((completed_benchmarks++)) || true
        done
    fi

    # --- Rust reference ---
    if [[ "$NO_REFERENCE" != "1" && "$HAS_RUSTC" == "1" && -f "$REF_DIR/rust/$prog.rs" ]]; then
        log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: rust-O...                       "
        rust_exe="$TMPDIR_BENCH/rust_$prog"
        if "$RUSTC" -O -o "$rust_exe" "$REF_DIR/rust/$prog.rs" 2>/dev/null; then
            result=$(time_executable "$rust_exe" "$ITERATIONS" "$WARMUP")
            [[ -n "$modes_json" ]] && modes_json="$modes_json,"
            modes_json="$modes_json\"rust-O\":$result"
        else
            [[ -n "$modes_json" ]] && modes_json="$modes_json,"
            modes_json="$modes_json\"rust-O\":{\"success\":false,\"skip_reason\":\"compilation failed\"}"
        fi
        ((completed_benchmarks++)) || true
    fi

    # --- Lua reference ---
    if [[ "$NO_REFERENCE" != "1" && "$HAS_LUA" == "1" && -f "$REF_DIR/lua/$prog.lua" ]]; then
        log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: lua...                          "
        # Lua scripts exit via os.exit(); capture exit code just like executables.
        result=$(time_executable lua "$ITERATIONS" "$WARMUP" "$REF_DIR/lua/$prog.lua")
        [[ -n "$modes_json" ]] && modes_json="$modes_json,"
        modes_json="$modes_json\"lua\":$result"
        ((completed_benchmarks++)) || true
    fi

    # --- Python reference ---
    if [[ "$NO_REFERENCE" != "1" && "$HAS_PYTHON3" == "1" && -f "$REF_DIR/python/$prog.py" ]]; then
        log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: python3...                     "
        result=$(time_python "$REF_DIR/python/$prog.py" "$ITERATIONS" "$WARMUP")
        [[ -n "$modes_json" ]] && modes_json="$modes_json,"
        modes_json="$modes_json\"python3\":$result"
        ((completed_benchmarks++)) || true
    fi

    # --- Java reference ---
    if [[ "$NO_REFERENCE" != "1" && "$HAS_JAVA" == "1" ]]; then
        java_class=$(snake_to_pascal "$prog")
        java_file="$REF_DIR/java/$java_class.java"
        if [[ -f "$java_file" ]]; then
            log_progress "\r  [$completed_benchmarks/$total_benchmarks] $prog: java...                        "
            java_build_dir="$TMPDIR_BENCH/java"
            mkdir -p "$java_build_dir"
            if "$JAVAC" -d "$java_build_dir" "$java_file" 2>/dev/null; then
                result=$(time_executable "$JAVA" "$ITERATIONS" "$WARMUP" -cp "$java_build_dir" "$java_class")
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"java\":$result"
            else
                [[ -n "$modes_json" ]] && modes_json="$modes_json,"
                modes_json="$modes_json\"java\":{\"success\":false,\"skip_reason\":\"compilation failed\"}"
            fi
            ((completed_benchmarks++)) || true
        fi
    fi

    # Build per-program JSON
    PROGRAM_RESULTS+=("{\"program\":\"$prog\",\"modes\":{$modes_json}}")
done

log_progress "\r  [$total_benchmarks/$total_benchmarks] Done!                                          \n"

# ============================================================================
# Assemble final JSON
# ============================================================================
benchmarks_array=$(printf '%s\n' "${PROGRAM_RESULTS[@]}" | paste -sd',' -)

RESULT_JSON=$(python3 -c "
import json
metadata = {
    'timestamp': '$TIMESTAMP',
    'commit': '$COMMIT',
    'branch': '$BRANCH',
    'platform': '$PLATFORM',
    'cpu': '''$CPU_MODEL''',
    'iterations': $ITERATIONS,
    'warmup': $WARMUP
}
benchmarks = json.loads('[$benchmarks_array]')
result = {'version': 1, 'metadata': metadata, 'benchmarks': benchmarks}
print(json.dumps(result, separators=(',',':')))" 2>/dev/null)

if [[ -z "$RESULT_JSON" ]]; then
    echo "error: failed to assemble JSON results" >&2
    exit 1
fi

# ============================================================================
# Store results
# ============================================================================
RESULTS_FILE="$ROOT_DIR/benchmarks/results.jsonl"
BASELINE_FILE="$ROOT_DIR/benchmarks/baseline.jsonl"

# Append to results file
echo "$RESULT_JSON" >> "$RESULTS_FILE"
log "  Results appended to: ${DIM}benchmarks/results.jsonl${NC}"

# Set baseline if requested
if [[ "$SET_BASELINE" == "1" ]]; then
    echo "$RESULT_JSON" > "$BASELINE_FILE"
    log "  ${GREEN}Baseline saved to: benchmarks/baseline.jsonl${NC}"
fi

# ============================================================================
# Print results summary table
# ============================================================================
log ""
log "${BOLD}  RESULTS (median time in ms, lower is better)${NC}"
log ""

python3 -c "
import json, sys

data = json.loads('''$RESULT_JSON''')

# Ordered mode list
mode_order = [
    'bc-switch', 'bc-threaded',
    'native-arm64', 'native-x86_64',
    'c-O0', 'c-O2', 'c-O3',
    'rust-O', 'lua',
    'python3', 'java'
]

# Collect all modes that appear in any benchmark
active_modes = []
for mode in mode_order:
    for b in data['benchmarks']:
        m = b['modes'].get(mode, {})
        if m.get('success', False):
            active_modes.append(mode)
            break

# Print header
hdr = f'  {\"Program\":<18}'
for mode in active_modes:
    hdr += f'{mode:>14}'
print(hdr)
print('  ' + '-' * (18 + 14 * len(active_modes)))

# Print rows
for b in data['benchmarks']:
    row = f'  {b[\"program\"]:<18}'
    for mode in active_modes:
        m = b['modes'].get(mode, {})
        if m.get('success', False):
            t = m['time_ms']
            if t >= 1000:
                row += f'{t/1000:>12.2f}s '
            else:
                row += f'{t:>11.1f}ms '
        else:
            reason = m.get('skip_reason', 'failed')
            row += f'{\"--\":>14}'
    print(row)

print()
" 2>/dev/null

# ============================================================================
# Auto-compare against baseline
# ============================================================================
if [[ -f "$BASELINE_FILE" && "$SET_BASELINE" != "1" ]]; then
    log ""
    log "${BOLD}  COMPARISON vs BASELINE${NC}"
    log ""

    # Write head JSON to temp file for comparison
    head_tmp="$TMPDIR_BENCH/compare_head.jsonl"
    echo "$RESULT_JSON" > "$head_tmp"

    COMPARE_SCRIPT="$ROOT_DIR/scripts/benchmark_compare.sh"
    if [[ -x "$COMPARE_SCRIPT" ]]; then
        "$COMPARE_SCRIPT" "$BASELINE_FILE" "$head_tmp" || true
    fi
fi

log "${BOLD}══════════════════════════════════════════════════════════════════════════${NC}"
log ""
