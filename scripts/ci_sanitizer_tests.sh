#!/usr/bin/env bash
# Script: ci_sanitizer_tests.sh
# Purpose: Run namespace tests with ASan and UBSan enabled
# Exit code: 0 if all pass, non-zero if any fail

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR_BASE="${BUILD_DIR:-${REPO_ROOT}/build}"

echo -e "${BLUE}======================================"
echo "Namespace Tests with Sanitizers"
echo "======================================${NC}"
echo ""

# Only run on Clang (sanitizers work best with Clang)
if [[ "${CXX:-}" != *"clang"* ]] && [[ "$(basename $(which c++ 2>/dev/null || echo clang++))" != *"clang"* ]]; then
  echo -e "${YELLOW}⊘ SKIP${NC}: Sanitizers require Clang compiler"
  echo "Current compiler: ${CXX:-$(which c++ 2>/dev/null || echo 'unknown')}"
  exit 0
fi

echo "Compiler: ${CXX:-$(which c++ 2>/dev/null)}"
echo ""

FAILED_SANITIZERS=0

# ============================================================================
# Test 1: AddressSanitizer (ASan)
# ============================================================================

echo -e "${BLUE}[1/2]${NC} Testing with AddressSanitizer (ASan)..."
ASAN_BUILD_DIR="${BUILD_DIR_BASE}_asan"

echo "Configuring with ASan..."
if cmake -S "${REPO_ROOT}" -B "${ASAN_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DIL_SANITIZE_ADDRESS=ON \
    > /tmp/asan_config.log 2>&1; then
  echo -e "${GREEN}✓${NC} Configuration succeeded"
else
  echo -e "${RED}✗${NC} Configuration failed"
  cat /tmp/asan_config.log
  FAILED_SANITIZERS=$((FAILED_SANITIZERS + 1))
fi

if [[ $FAILED_SANITIZERS -eq 0 ]]; then
  echo "Building with ASan..."
  if cmake --build "${ASAN_BUILD_DIR}" -j > /tmp/asan_build.log 2>&1; then
    echo -e "${GREEN}✓${NC} Build succeeded"
  else
    echo -e "${RED}✗${NC} Build failed"
    tail -50 /tmp/asan_build.log
    FAILED_SANITIZERS=$((FAILED_SANITIZERS + 1))
  fi
fi

if [[ $FAILED_SANITIZERS -eq 0 ]]; then
  echo "Running namespace tests with ASan..."
  if BUILD_DIR="${ASAN_BUILD_DIR}" "${SCRIPT_DIR}/ci_namespace_tests.sh" > /tmp/asan_tests.log 2>&1; then
    echo -e "${GREEN}✓ PASS${NC}: All tests passed with ASan"
  else
    echo -e "${RED}✗ FAIL${NC}: Tests failed with ASan"
    cat /tmp/asan_tests.log
    FAILED_SANITIZERS=$((FAILED_SANITIZERS + 1))
  fi
fi
echo ""

# ============================================================================
# Test 2: UndefinedBehaviorSanitizer (UBSan)
# ============================================================================

echo -e "${BLUE}[2/2]${NC} Testing with UndefinedBehaviorSanitizer (UBSan)..."
UBSAN_BUILD_DIR="${BUILD_DIR_BASE}_ubsan"

echo "Configuring with UBSan..."
if cmake -S "${REPO_ROOT}" -B "${UBSAN_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DIL_SANITIZE_UNDEFINED=ON \
    > /tmp/ubsan_config.log 2>&1; then
  echo -e "${GREEN}✓${NC} Configuration succeeded"
else
  echo -e "${RED}✗${NC} Configuration failed"
  cat /tmp/ubsan_config.log
  FAILED_SANITIZERS=$((FAILED_SANITIZERS + 1))
fi

if [[ $FAILED_SANITIZERS -eq 0 ]]; then
  echo "Building with UBSan..."
  if cmake --build "${UBSAN_BUILD_DIR}" -j > /tmp/ubsan_build.log 2>&1; then
    echo -e "${GREEN}✓${NC} Build succeeded"
  else
    echo -e "${RED}✗${NC} Build failed"
    tail -50 /tmp/ubsan_build.log
    FAILED_SANITIZERS=$((FAILED_SANITIZERS + 1))
  fi
fi

if [[ $FAILED_SANITIZERS -eq 0 ]]; then
  echo "Running namespace tests with UBSan..."
  if BUILD_DIR="${UBSAN_BUILD_DIR}" "${SCRIPT_DIR}/ci_namespace_tests.sh" > /tmp/ubsan_tests.log 2>&1; then
    echo -e "${GREEN}✓ PASS${NC}: All tests passed with UBSan"
  else
    echo -e "${RED}✗ FAIL${NC}: Tests failed with UBSan"
    cat /tmp/ubsan_tests.log
    FAILED_SANITIZERS=$((FAILED_SANITIZERS + 1))
  fi
fi
echo ""

# ============================================================================
# Summary
# ============================================================================

echo -e "${BLUE}======================================"
echo "Summary"
echo "======================================${NC}"
echo ""

if [[ $FAILED_SANITIZERS -eq 0 ]]; then
  echo -e "${GREEN}✓ ALL SANITIZER TESTS PASSED${NC}"
  echo ""
  echo "No memory errors or undefined behavior detected:"
  echo "  • ASan: Clean"
  echo "  • UBSan: Clean"
  exit 0
else
  echo -e "${RED}✗ SANITIZER TESTS FAILED${NC}"
  echo ""
  echo "Please investigate and fix the issues above."
  exit 1
fi
