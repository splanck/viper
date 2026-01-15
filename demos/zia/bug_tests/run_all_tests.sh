#!/bin/bash
# Run all bug fix tests

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VIPER="$SCRIPT_DIR/../../../build/src/tools/viper/viper"
ILRUN="$SCRIPT_DIR/../../../build/src/tools/ilrun/ilrun"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "ViperLang Bug Fix Test Suite"
echo "========================================"
echo ""

TOTAL=0
PASSED=0
FAILED=0

run_test() {
    local name="$1"
    local file="$2"

    TOTAL=$((TOTAL + 1))
    echo "----------------------------------------"
    echo -e "${YELLOW}Running: $name${NC}"
    echo "----------------------------------------"

    # Compile
    local ilfile="/tmp/$(basename "$file" .viper)"
    if ! "$VIPER" "$file" -o "$ilfile" 2>&1; then
        echo -e "${RED}FAIL: Compilation error${NC}"
        FAILED=$((FAILED + 1))
        return 1
    fi

    # Run
    if "$ILRUN" "$ilfile" 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "${RED}FAIL: Runtime error${NC}"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

# Run each test
run_test "Bug #3: String.Length property" "$SCRIPT_DIR/test_bug3_string_length.viper"
echo ""
run_test "Bug #5: else-if chain codegen" "$SCRIPT_DIR/test_bug5_elseif_chain.viper"
echo ""
run_test "Bug #7: List[String] type fix (simple)" "$SCRIPT_DIR/test_bug7_list_simple.viper"

echo ""
echo "========================================"
echo "Test Results: $PASSED/$TOTAL passed"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}$FAILED test(s) FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}All tests PASSED!${NC}"
    exit 0
fi
