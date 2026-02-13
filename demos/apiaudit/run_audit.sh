#!/bin/bash
# Run all API audit demos and capture results
# Usage: ./run_audit.sh [vm|native|both]

VIPER="build/src/tools/viper/viper"
AUDIT_DIR="demos/apiaudit"
MODE="${1:-vm}"
TIMEOUT=10
RESULTS_DIR="/tmp/apiaudit_results"
mkdir -p "$RESULTS_DIR"

pass=0
fail=0
crash=0
timeout_count=0

run_demo() {
    local file="$1"
    local mode="$2"
    local relpath="${file#$AUDIT_DIR/}"
    local basename="${relpath%.*}"
    local ext="${file##*.}"
    local outfile="$RESULTS_DIR/${basename//\//_}_${ext}_${mode}.txt"
    local errfile="$RESULTS_DIR/${basename//\//_}_${ext}_${mode}.err"

    if [ "$mode" = "native" ]; then
        local result
        result=$(timeout $TIMEOUT "$VIPER" run --native "$file" 2>"$errfile")
    else
        local result
        result=$(timeout $TIMEOUT "$VIPER" run "$file" 2>"$errfile")
    fi
    local exit_code=$?

    echo "$result" > "$outfile"

    if [ $exit_code -eq 124 ]; then
        echo "TIMEOUT $relpath ($mode)"
        ((timeout_count++))
    elif [ $exit_code -ne 0 ]; then
        local errmsg=$(head -3 "$errfile" 2>/dev/null)
        echo "FAIL    $relpath ($mode) [exit=$exit_code] $errmsg"
        ((fail++))
    else
        local lines=$(echo "$result" | wc -l | tr -d ' ')
        echo "PASS    $relpath ($mode) [$lines lines]"
        ((pass++))
    fi
}

echo "=== Viper API Audit Test Runner ==="
echo "Mode: $MODE"
echo ""

for demo in $(find "$AUDIT_DIR" -type f \( -name "*.zia" -o -name "*.bas" \) | sort); do
    if [ "$MODE" = "both" ]; then
        run_demo "$demo" "vm"
        run_demo "$demo" "native"
    else
        run_demo "$demo" "$MODE"
    fi
done

echo ""
echo "=== Summary ==="
echo "PASS:    $pass"
echo "FAIL:    $fail"
echo "TIMEOUT: $timeout_count"
echo "Total:   $((pass + fail + timeout_count))"
