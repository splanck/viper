#!/usr/bin/env bash
# Script: fuzz_smoke.sh
# Purpose: Build and time-box local libFuzzer smoke runs over committed corpora.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="${ZANNA_FUZZ_BUILD_DIR:-${ROOT_DIR}/build-fuzz}"
SECONDS_PER_TARGET="${ZANNA_FUZZ_SECONDS:-5}"
JOBS="${ZANNA_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
MODE="run"

usage() {
    cat <<'EOF'
Usage: scripts/fuzz_smoke.sh [options]

Options:
  --list              Print discovered fuzz targets and exit
  --self-test         Verify target discovery and corpus directories
  --build-dir DIR     Fuzz build directory (default: build-fuzz)
  --seconds N         Max seconds per target (default: 5)
  -h, --help          Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --list) MODE="list"; shift ;;
        --self-test) MODE="self-test"; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --seconds) SECONDS_PER_TARGET="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

discover_targets() {
    sed -n -E 's/^[[:space:]]*zanna_add_(3d_loader_)?fuzzer\((fuzz_[^[:space:]\)]+).*/\2/p' \
        "$ROOT_DIR/src/tests/fuzz/CMakeLists.txt" | sort -u
}

corpus_for() {
    local target="$1"
    local name="${target#fuzz_}"
    echo "$ROOT_DIR/src/tests/fuzz/corpus/$name"
}

targets=()
while IFS= read -r target; do
    [[ -n "$target" ]] && targets+=("$target")
done < <(discover_targets)

if [[ "$MODE" == "list" ]]; then
    printf '%s\n' "${targets[@]}"
    exit 0
fi

if [[ ${#targets[@]} -eq 0 ]]; then
    echo "error: no fuzz targets discovered" >&2
    exit 1
fi

missing=0
for target in "${targets[@]}"; do
    corpus="$(corpus_for "$target")"
    if [[ ! -d "$corpus" ]]; then
        echo "missing corpus: $target -> $corpus" >&2
        missing=1
    fi
done

if [[ "$missing" != "0" ]]; then
    exit 1
fi

if [[ "$MODE" == "self-test" ]]; then
    echo "fuzz smoke self-test: ${#targets[@]} targets discovered with corpora"
    exit 0
fi

if ! command -v clang++ >/dev/null 2>&1; then
    echo "error: clang++ is required for libFuzzer builds" >&2
    exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DZANNA_ENABLE_FUZZ=ON \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++
cmake --build "$BUILD_DIR" -j"$JOBS" --target "${targets[@]}"

for target in "${targets[@]}"; do
    corpus="$(corpus_for "$target")"
    bin="$BUILD_DIR/src/tests/$target"
    if [[ ! -x "$bin" ]]; then
        bin="$BUILD_DIR/src/tests/fuzz/$target"
    fi
    if [[ ! -x "$bin" ]]; then
        echo "error: built fuzzer not found for $target" >&2
        exit 1
    fi
    echo "[fuzz] $target ($SECONDS_PER_TARGET seconds)"
    "$bin" "$corpus" -max_total_time="$SECONDS_PER_TARGET" -print_final_stats=1
done

echo "fuzz smoke: ok"
