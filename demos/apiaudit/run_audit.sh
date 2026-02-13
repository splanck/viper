#!/bin/bash
# run_audit.sh — Run a Zia/BASIC demo pair in VM and native modes, compare outputs
# Usage: ./run_audit.sh <zia_file> <bas_file>
#   or:  ./run_audit.sh <directory>  (runs all pairs in that directory)

set -euo pipefail

VIPER_DIR="/Users/stephen/git/viper"
ZIA="${VIPER_DIR}/build/src/tools/zia/zia"
VBASIC="${VIPER_DIR}/build/src/tools/vbasic/vbasic"
VIPER="${VIPER_DIR}/build/src/tools/viper/viper"
TMPDIR="${TMPDIR:-/tmp}/viper_audit"
mkdir -p "$TMPDIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

run_one() {
    local zia_file="$1"
    local bas_file="$2"
    local name
    name=$(basename "$zia_file" .zia)

    echo "=== $name ==="

    local zia_vm_out="$TMPDIR/${name}_zia_vm.out"
    local bas_vm_out="$TMPDIR/${name}_bas_vm.out"
    local zia_nat_out="$TMPDIR/${name}_zia_nat.out"
    local bas_nat_out="$TMPDIR/${name}_bas_nat.out"

    # Zia VM
    local zia_vm_rc=0
    timeout 10 "$ZIA" "$zia_file" > "$zia_vm_out" 2>&1 || zia_vm_rc=$?
    if [ $zia_vm_rc -ne 0 ]; then
        echo -e "  ${RED}Zia VM: FAIL (exit $zia_vm_rc)${NC}"
    else
        echo -e "  ${GREEN}Zia VM: OK${NC}"
    fi

    # BASIC VM
    local bas_vm_rc=0
    timeout 10 "$VBASIC" "$bas_file" > "$bas_vm_out" 2>&1 || bas_vm_rc=$?
    if [ $bas_vm_rc -ne 0 ]; then
        echo -e "  ${RED}BASIC VM: FAIL (exit $bas_vm_rc)${NC}"
    else
        echo -e "  ${GREEN}BASIC VM: OK${NC}"
    fi

    # Zia Native
    local zia_nat_rc=0
    local zia_binary="$TMPDIR/${name}_zia"
    if timeout 15 "$VIPER" build "$zia_file" -o "$zia_binary" --arch arm64 > "$TMPDIR/${name}_zia_build.log" 2>&1; then
        timeout 10 "$zia_binary" > "$zia_nat_out" 2>&1 || zia_nat_rc=$?
        if [ $zia_nat_rc -ne 0 ]; then
            echo -e "  ${RED}Zia Native: CRASH (exit $zia_nat_rc)${NC}"
        else
            echo -e "  ${GREEN}Zia Native: OK${NC}"
        fi
    else
        echo -e "  ${RED}Zia Native: BUILD FAIL${NC}"
        zia_nat_rc=1
    fi

    # BASIC Native
    local bas_nat_rc=0
    local bas_binary="$TMPDIR/${name}_bas"
    if timeout 15 "$VIPER" build "$bas_file" -o "$bas_binary" --arch arm64 > "$TMPDIR/${name}_bas_build.log" 2>&1; then
        timeout 10 "$bas_binary" > "$bas_nat_out" 2>&1 || bas_nat_rc=$?
        if [ $bas_nat_rc -ne 0 ]; then
            echo -e "  ${RED}BASIC Native: CRASH (exit $bas_nat_rc)${NC}"
        else
            echo -e "  ${GREEN}BASIC Native: OK${NC}"
        fi
    else
        echo -e "  ${RED}BASIC Native: BUILD FAIL${NC}"
        bas_nat_rc=1
    fi

    # Compare outputs
    if [ $zia_vm_rc -eq 0 ] && [ $bas_vm_rc -eq 0 ]; then
        if ! diff -q "$zia_vm_out" "$bas_vm_out" > /dev/null 2>&1; then
            echo -e "  ${YELLOW}DIFF: Zia VM vs BASIC VM${NC}"
            diff "$zia_vm_out" "$bas_vm_out" | head -10
        fi
    fi

    if [ $zia_vm_rc -eq 0 ] && [ $zia_nat_rc -eq 0 ] && [ -f "$zia_nat_out" ]; then
        if ! diff -q "$zia_vm_out" "$zia_nat_out" > /dev/null 2>&1; then
            echo -e "  ${YELLOW}DIFF: Zia VM vs Zia Native${NC}"
            diff "$zia_vm_out" "$zia_nat_out" | head -10
        fi
    fi

    if [ $bas_vm_rc -eq 0 ] && [ $bas_nat_rc -eq 0 ] && [ -f "$bas_nat_out" ]; then
        if ! diff -q "$bas_vm_out" "$bas_nat_out" > /dev/null 2>&1; then
            echo -e "  ${YELLOW}DIFF: BASIC VM vs BASIC Native${NC}"
            diff "$bas_vm_out" "$bas_nat_out" | head -10
        fi
    fi

    echo ""
}

if [ $# -eq 1 ] && [ -d "$1" ]; then
    for zia_file in "$1"/*_demo.zia; do
        [ -f "$zia_file" ] || continue
        bas_file="${zia_file%.zia}.bas"
        [ -f "$bas_file" ] || continue
        run_one "$zia_file" "$bas_file"
    done
elif [ $# -eq 2 ]; then
    run_one "$1" "$2"
else
    echo "Usage: $0 <zia_file> <bas_file>"
    echo "   or: $0 <directory>"
    exit 1
fi
