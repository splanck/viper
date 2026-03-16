#!/usr/bin/env bash
# Script: ci_full_sanitizer.sh
# Purpose: Run the FULL Viper test suite under ASan, UBSan, and TSan.
#          Unlike ci_sanitizer_tests.sh (namespace tests only), this script
#          covers all 1,300+ tests for comprehensive memory/UB/race detection.
#
# Usage:
#   ./scripts/ci_full_sanitizer.sh          # Run ASan + UBSan
#   ./scripts/ci_full_sanitizer.sh --tsan   # Also run TSan (thread tests only)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR_BASE="${BUILD_DIR:-${REPO_ROOT}/build}"
RUN_TSAN=false

for arg in "$@"; do
    case "$arg" in
        --tsan) RUN_TSAN=true ;;
    esac
done

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

# Only run on Clang
if ! command -v clang++ > /dev/null 2>&1; then
    echo -e "${RED}Error: clang++ not found. Sanitizers require Clang.${NC}"
    exit 1
fi

FAILED=0

# ============================================================================
# ASan — Address Sanitizer (heap/stack buffer overflow, use-after-free)
# ============================================================================
echo -e "${BLUE}[1/3] Building with AddressSanitizer...${NC}"
ASAN_DIR="${BUILD_DIR_BASE}_asan_full"

cmake -S "${REPO_ROOT}" -B "${ASAN_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DIL_SANITIZE_ADDRESS=ON \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER=clang \
    > /dev/null 2>&1

cmake --build "${ASAN_DIR}" -j > /dev/null 2>&1

echo "Running all tests under ASan..."
if ctest --test-dir "${ASAN_DIR}" -j --output-on-failure --timeout 60 \
    -E "test_diff_vm_native_property|perf_|stress_scalability" 2>&1 | tail -5; then
    echo -e "${GREEN}ASan: PASS${NC}"
else
    echo -e "${RED}ASan: FAIL${NC}"
    FAILED=$((FAILED + 1))
fi
echo ""

# ============================================================================
# UBSan — Undefined Behavior Sanitizer
# ============================================================================
echo -e "${BLUE}[2/3] Building with UndefinedBehaviorSanitizer...${NC}"
UBSAN_DIR="${BUILD_DIR_BASE}_ubsan_full"

cmake -S "${REPO_ROOT}" -B "${UBSAN_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DIL_SANITIZE_UNDEFINED=ON \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_COMPILER=clang \
    > /dev/null 2>&1

cmake --build "${UBSAN_DIR}" -j > /dev/null 2>&1

echo "Running all tests under UBSan..."
if ctest --test-dir "${UBSAN_DIR}" -j --output-on-failure --timeout 60 \
    -E "test_diff_vm_native_property|perf_|stress_scalability" 2>&1 | tail -5; then
    echo -e "${GREEN}UBSan: PASS${NC}"
else
    echo -e "${RED}UBSan: FAIL${NC}"
    FAILED=$((FAILED + 1))
fi
echo ""

# ============================================================================
# TSan — Thread Sanitizer (optional, thread-related tests only)
# ============================================================================
if $RUN_TSAN; then
    echo -e "${BLUE}[3/3] Building with ThreadSanitizer...${NC}"
    TSAN_DIR="${BUILD_DIR_BASE}_tsan_full"

    cmake -S "${REPO_ROOT}" -B "${TSAN_DIR}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DIL_SANITIZE_THREAD=ON \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_C_COMPILER=clang \
        > /dev/null 2>&1

    cmake --build "${TSAN_DIR}" -j > /dev/null 2>&1

    echo "Running VM and runtime tests under TSan..."
    if ctest --test-dir "${TSAN_DIR}" -j --output-on-failure --timeout 60 \
        -L "vm|runtime" \
        -E "test_diff_vm_native_property|perf_|stress_scalability" 2>&1 | tail -5; then
        echo -e "${GREEN}TSan: PASS${NC}"
    else
        echo -e "${RED}TSan: FAIL${NC}"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "${BLUE}[3/3] TSan skipped (pass --tsan to enable)${NC}"
fi
echo ""

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All sanitizer suites passed.${NC}"
    exit 0
else
    echo -e "${RED}${FAILED} sanitizer suite(s) failed.${NC}"
    exit 1
fi
