#!/bin/bash
#
# Viper Runtime Test Suite - Master Runner
#
# This script runs all runtime tests and reports results.
# Tests are organized by logical area (math, string, collections, etc.)
#
# Usage:
#   ./run_all_runtime_tests.sh           # Run all tests
#   ./run_all_runtime_tests.sh --quick   # Run only quick tests (skip file/network)
#   ./run_all_runtime_tests.sh --verbose # Show full test output
#
# Prerequisites:
#   - Built zia compiler in ../../build/zia or in PATH
#

set -e

# Script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Find zia compiler
if [ -f "$PROJECT_ROOT/build/zia" ]; then
    ZIA="$PROJECT_ROOT/build/zia"
elif command -v zia &> /dev/null; then
    ZIA="$(command -v zia)"
else
    echo "ERROR: Cannot find zia compiler"
    echo "Build it with: cmake -S . -B build && cmake --build build -j"
    exit 1
fi

# Parse arguments
VERBOSE=0
QUICK=0
for arg in "$@"; do
    case $arg in
        --verbose|-v)
            VERBOSE=1
            ;;
        --quick|-q)
            QUICK=1
            ;;
        --help|-h)
            echo "Usage: $0 [--verbose|-v] [--quick|-q]"
            echo ""
            echo "Options:"
            echo "  --verbose, -v  Show full test output"
            echo "  --quick, -q    Skip slow tests (file I/O, network)"
            echo "  --help, -h     Show this help"
            exit 0
            ;;
    esac
done

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
PASSED=0
FAILED=0
SKIPPED=0
declare -a FAILED_TESTS=()

# Run a single test
run_test() {
    local name="$1"
    local test_file="$2"
    local skip="${3:-0}"

    printf "  %-25s " "$name"

    if [ ! -f "$test_file" ]; then
        printf "${YELLOW}SKIP${NC} (file not found)\n"
        ((SKIPPED++))
        return
    fi

    if [ "$skip" -eq 1 ]; then
        printf "${YELLOW}SKIP${NC} (--quick mode)\n"
        ((SKIPPED++))
        return
    fi

    # Run test and capture output
    local output
    local exit_code=0
    output=$("$ZIA" "$test_file" 2>&1) || exit_code=$?

    if [ $VERBOSE -eq 1 ]; then
        echo ""
        echo "$output"
        echo ""
    fi

    # Check for success
    if echo "$output" | grep -q "RESULT: ok"; then
        printf "${GREEN}PASS${NC}\n"
        ((PASSED++))
    else
        printf "${RED}FAIL${NC}\n"
        ((FAILED++))
        FAILED_TESTS+=("$name")

        # Show last few lines on failure
        if [ $VERBOSE -eq 0 ]; then
            echo "$output" | tail -10 | sed 's/^/    /'
        fi
    fi
}

# Print header
echo ""
echo -e "${BLUE}╔══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║              Viper Runtime Test Suite                     ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Using zia: $ZIA"
echo ""

# Math & Numeric Tests
echo -e "${BLUE}[Math & Numeric]${NC}"
run_test "Math Functions" "$SCRIPT_DIR/test_math.zia"
run_test "Bit Operations" "$SCRIPT_DIR/test_bits.zia"
run_test "Random Numbers" "$SCRIPT_DIR/test_random.zia"
echo ""

# String Tests
echo -e "${BLUE}[String Operations]${NC}"
run_test "String Functions" "$SCRIPT_DIR/test_string.zia"
echo ""

# Collection Tests
echo -e "${BLUE}[Collections]${NC}"
run_test "List" "$SCRIPT_DIR/test_list.zia"
run_test "Map" "$SCRIPT_DIR/test_map.zia"
run_test "Other Collections" "$SCRIPT_DIR/test_collections.zia"
run_test "Bytes" "$SCRIPT_DIR/test_bytes.zia"
echo ""

# I/O Tests
echo -e "${BLUE}[Input/Output]${NC}"
run_test "Terminal I/O" "$SCRIPT_DIR/test_terminal.zia"
run_test "File I/O" "$SCRIPT_DIR/test_file.zia" $QUICK
echo ""

# System Tests
echo -e "${BLUE}[System]${NC}"
run_test "DateTime" "$SCRIPT_DIR/test_datetime.zia"
run_test "Environment" "$SCRIPT_DIR/test_environment.zia"
run_test "Diagnostics" "$SCRIPT_DIR/test_diagnostics.zia"
echo ""

# Security Tests
echo -e "${BLUE}[Crypto]${NC}"
run_test "Cryptography" "$SCRIPT_DIR/test_crypto.zia"
echo ""

# Graphics Tests
echo -e "${BLUE}[Graphics]${NC}"
run_test "Graphics/Color/Pixels" "$SCRIPT_DIR/test_graphics.zia"
run_test "GUI (docs only)" "$SCRIPT_DIR/test_gui.zia"
echo ""

# Print summary
echo -e "${BLUE}════════════════════════════════════════════════════════════${NC}"
echo ""
echo "Summary:"
printf "  ${GREEN}Passed:${NC}  %d\n" $PASSED
printf "  ${RED}Failed:${NC}  %d\n" $FAILED
printf "  ${YELLOW}Skipped:${NC} %d\n" $SKIPPED
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed tests:${NC}"
    for test in "${FAILED_TESTS[@]}"; do
        echo "  - $test"
    done
    echo ""
    echo -e "${RED}TESTS FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
fi
