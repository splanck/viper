#!/usr/bin/env bash
# Script: coverage.sh
# Purpose: Build Viper with Clang source-based coverage and emit local reports.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="${VIPER_COVERAGE_BUILD_DIR:-${ROOT_DIR}/build-coverage}"
REPORT_DIR="${VIPER_COVERAGE_REPORT_DIR:-${ROOT_DIR}/coverage}"
JOBS="${VIPER_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
SELF_TEST=false

usage() {
    cat <<'EOF'
Usage: scripts/coverage.sh [options]

Options:
  --build-dir DIR      Coverage build directory (default: build-coverage)
  --report-dir DIR     Report output directory (default: coverage)
  --self-test          Verify required tools and CMake option wiring only
  -h, --help           Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --report-dir) REPORT_DIR="$2"; shift 2 ;;
        --self-test) SELF_TEST=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

need_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: required tool not found: $1" >&2
        exit 1
    fi
}

need_tool clang
need_tool clang++
need_tool cmake
need_tool ctest

resolve_llvm_tool() {
    local tool="$1"
    if command -v "$tool" >/dev/null 2>&1; then
        command -v "$tool"
        return 0
    fi
    if command -v xcrun >/dev/null 2>&1; then
        xcrun -f "$tool" 2>/dev/null && return 0
    fi
    return 1
}

LLVM_PROFDATA="$(resolve_llvm_tool llvm-profdata || true)"
LLVM_COV="$(resolve_llvm_tool llvm-cov || true)"
if [[ -z "$LLVM_PROFDATA" ]]; then
    echo "error: required tool not found: llvm-profdata" >&2
    exit 1
fi
if [[ -z "$LLVM_COV" ]]; then
    echo "error: required tool not found: llvm-cov" >&2
    exit 1
fi

if $SELF_TEST; then
    if ! grep -q 'VIPER_ENABLE_COVERAGE' "${ROOT_DIR}/CMakeLists.txt"; then
        echo "coverage self-test: missing VIPER_ENABLE_COVERAGE CMake option" >&2
        exit 1
    fi
    echo "coverage self-test: ok"
    exit 0
fi

rm -rf "$REPORT_DIR"
mkdir -p "$REPORT_DIR/raw" "$REPORT_DIR/html"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DVIPER_ENABLE_COVERAGE=ON \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++

cmake --build "$BUILD_DIR" -j"$JOBS"

export LLVM_PROFILE_FILE="${REPORT_DIR}/raw/%p-%m.profraw"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
    -E "requires_display|perf_|stress_scalability"

"$LLVM_PROFDATA" merge -sparse "${REPORT_DIR}"/raw/*.profraw -o "${REPORT_DIR}/coverage.profdata"

objects=()
while IFS= read -r object; do
    objects+=("$object")
done < <(
    find "$BUILD_DIR" -type f \( -perm -111 -o -name '*.dylib' -o -name '*.so' \) \
        ! -path '*/CMakeFiles/*' \
        ! -path '*/Testing/*' \
        | sort
)

if [[ ${#objects[@]} -eq 0 ]]; then
    echo "error: no coverage objects found in $BUILD_DIR" >&2
    exit 1
fi

primary="${objects[0]}"
object_args=()
for ((i = 1; i < ${#objects[@]}; i++)); do
    object_args+=("-object=${objects[$i]}")
done

"$LLVM_COV" report "$primary" "${object_args[@]}" \
    -instr-profile="${REPORT_DIR}/coverage.profdata" \
    "${ROOT_DIR}/src" \
    > "${REPORT_DIR}/summary.txt"

"$LLVM_COV" show "$primary" "${object_args[@]}" \
    -instr-profile="${REPORT_DIR}/coverage.profdata" \
    -format=html \
    -output-dir="${REPORT_DIR}/html" \
    "${ROOT_DIR}/src"

awk -v root="${ROOT_DIR}/" '
    /^\/.*src\// {
        file=$1
        sub(root, "", file)
        split(file, parts, "/")
        if (parts[1] == "src" && parts[2] != "") {
            dir=parts[1] "/" parts[2]
            if (parts[3] != "") dir=dir "/" parts[3]
            pct=$10
            gsub(/%/, "", pct)
            if (pct ~ /^[0-9.]+$/) {
                sum[dir] += pct
                count[dir] += 1
            }
        }
    }
    END {
        print "Subsystem line-coverage rollup (unweighted average by file):"
        for (dir in sum) {
            printf "%7.2f%%  %s\n", sum[dir] / count[dir], dir
        }
    }
' "${REPORT_DIR}/summary.txt" | sort -n > "${REPORT_DIR}/subsystems.txt"

echo "Coverage summary: ${REPORT_DIR}/summary.txt"
echo "Subsystem rollup: ${REPORT_DIR}/subsystems.txt"
echo "HTML report:      ${REPORT_DIR}/html/index.html"
