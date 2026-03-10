#!/bin/bash
# check_codegen_opcode_completeness.sh
# Validates consistency between JSON encoding specs, MOpcode enums, and
# opcodeName() switch tables for both x86_64 and AArch64 backends.
# Exits 1 if any mismatches are found.
#
# Usage: ./scripts/check_codegen_opcode_completeness.sh

set -euo pipefail

ERRORS=0

# --- Helper: extract values between braces of 'enum class MOpcode {' ---
# Works with BSD and GNU sed/awk.
extract_enum_values() {
    local file="$1"
    # Find the enum body and extract identifiers (strip comments, commas, whitespace)
    awk '/enum class MOpcode/,/^};/' "$file" \
        | grep -v 'enum class' \
        | grep -v '};' \
        | sed 's|//.*||' \
        | sed 's|/\*.*\*/||' \
        | tr ',' '\n' \
        | sed 's/^[[:space:]]*//' \
        | sed 's/[[:space:]]*$//' \
        | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' \
        | sort
}

# --- Helper: extract case labels from opcodeName() switch ---
extract_switch_cases() {
    local file="$1"
    grep 'case MOpcode::' "$file" \
        | sed 's/.*case MOpcode:://' \
        | sed 's/:.*//' \
        | sed 's/^[[:space:]]*//' \
        | sed 's/[[:space:]]*$//' \
        | sort
}

# --- Helper: extract opcode names from JSON (field name varies) ---
extract_json_opcodes() {
    local file="$1"
    local field="$2"
    # Use grep + sed to extract JSON string values (no Python dependency)
    grep "\"${field}\"" "$file" \
        | sed "s/.*\"${field}\"[[:space:]]*:[[:space:]]*\"//" \
        | sed 's/".*//' \
        | sort
}

# --- Helper: compare two sorted lists and report differences ---
compare_lists() {
    local label_a="$1"
    local label_b="$2"
    local file_a="$3"
    local file_b="$4"
    local context="$5"

    local missing_from_b
    missing_from_b=$(comm -23 "$file_a" "$file_b")
    local missing_from_a
    missing_from_a=$(comm -13 "$file_a" "$file_b")

    if [ -n "$missing_from_b" ]; then
        echo "  WARNING: In ${label_a} but missing from ${label_b}:"
        echo "$missing_from_b" | sed 's/^/    - /'
        ERRORS=$((ERRORS + 1))
    fi
    if [ -n "$missing_from_a" ]; then
        echo "  WARNING: In ${label_b} but missing from ${label_a}:"
        echo "$missing_from_a" | sed 's/^/    - /'
        ERRORS=$((ERRORS + 1))
    fi
}

echo "=== Codegen Opcode Completeness Check ==="
echo ""

# ============================================================
# x86_64 Backend
# ============================================================
echo "--- x86_64 Backend ---"

X86_ENUM_FILE="src/codegen/x86_64/MachineIR.hpp"
X86_SWITCH_FILE="src/codegen/x86_64/MachineIR.cpp"
X86_JSON_FILE="docs/spec/x86_64_encodings.json"

if [ ! -f "$X86_ENUM_FILE" ] || [ ! -f "$X86_SWITCH_FILE" ] || [ ! -f "$X86_JSON_FILE" ]; then
    echo "  ERROR: One or more x86_64 source files not found. Run from project root." >&2
    exit 2
fi

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

extract_enum_values "$X86_ENUM_FILE" > "$TMP_DIR/x86_enum.txt"
extract_switch_cases "$X86_SWITCH_FILE" > "$TMP_DIR/x86_switch.txt"
extract_json_opcodes "$X86_JSON_FILE" "opcode" > "$TMP_DIR/x86_json.txt"

x86_enum_count=$(wc -l < "$TMP_DIR/x86_enum.txt" | tr -d ' ')
x86_switch_count=$(wc -l < "$TMP_DIR/x86_switch.txt" | tr -d ' ')
x86_json_count=$(wc -l < "$TMP_DIR/x86_json.txt" | tr -d ' ')

echo "  Enum values:     $x86_enum_count"
echo "  Switch cases:    $x86_switch_count"
echo "  JSON entries:    $x86_json_count"

# Check 1: Enum vs Switch (every enum value should have an opcodeName case)
echo ""
echo "  [1] Enum vs opcodeName() switch:"
compare_lists "enum" "switch" "$TMP_DIR/x86_enum.txt" "$TMP_DIR/x86_switch.txt" "x86_64"
if [ $ERRORS -eq 0 ]; then
    echo "    OK — all enum values have switch cases"
fi
PREV_ERRORS=$ERRORS

# Check 2: JSON vs Enum (report differences; some opcodes are pseudo-only)
echo "  [2] JSON spec vs enum:"
json_missing_from_enum=$(comm -23 "$TMP_DIR/x86_json.txt" "$TMP_DIR/x86_enum.txt")
enum_missing_from_json=$(comm -13 "$TMP_DIR/x86_json.txt" "$TMP_DIR/x86_enum.txt")

if [ -n "$json_missing_from_enum" ]; then
    echo "    WARNING: In JSON but missing from enum:"
    echo "$json_missing_from_enum" | sed 's/^/      - /'
    ERRORS=$((ERRORS + 1))
fi
if [ -n "$enum_missing_from_json" ]; then
    echo "    NOTE: In enum but not in JSON (pseudo-ops or not yet documented):"
    echo "$enum_missing_from_json" | sed 's/^/      - /'
    # Not counted as error — pseudo-ops intentionally omitted from encoding spec
fi
if [ $ERRORS -eq $PREV_ERRORS ]; then
    echo "    OK — JSON entries are a subset of enum"
fi

echo ""

# ============================================================
# AArch64 Backend
# ============================================================
echo "--- AArch64 Backend ---"

A64_ENUM_FILE="src/codegen/aarch64/MachineIR.hpp"
A64_SWITCH_FILE="src/codegen/aarch64/MachineIR.cpp"
A64_JSON_FILE="docs/spec/aarch64_encodings.json"

if [ ! -f "$A64_ENUM_FILE" ] || [ ! -f "$A64_SWITCH_FILE" ] || [ ! -f "$A64_JSON_FILE" ]; then
    echo "  ERROR: One or more AArch64 source files not found. Run from project root." >&2
    exit 2
fi

extract_enum_values "$A64_ENUM_FILE" > "$TMP_DIR/a64_enum.txt"
extract_switch_cases "$A64_SWITCH_FILE" > "$TMP_DIR/a64_switch.txt"
extract_json_opcodes "$A64_JSON_FILE" "enum" > "$TMP_DIR/a64_json.txt"

a64_enum_count=$(wc -l < "$TMP_DIR/a64_enum.txt" | tr -d ' ')
a64_switch_count=$(wc -l < "$TMP_DIR/a64_switch.txt" | tr -d ' ')
a64_json_count=$(wc -l < "$TMP_DIR/a64_json.txt" | tr -d ' ')

echo "  Enum values:     $a64_enum_count"
echo "  Switch cases:    $a64_switch_count"
echo "  JSON entries:    $a64_json_count"

# Check 1: Enum vs Switch
echo ""
echo "  [1] Enum vs opcodeName() switch:"
PREV_ERRORS=$ERRORS
compare_lists "enum" "switch" "$TMP_DIR/a64_enum.txt" "$TMP_DIR/a64_switch.txt" "aarch64"
if [ $ERRORS -eq $PREV_ERRORS ]; then
    echo "    OK — all enum values have switch cases"
fi

# Check 2: JSON vs Enum
echo "  [2] JSON spec vs enum:"
PREV_ERRORS=$ERRORS
json_missing_from_enum=$(comm -23 "$TMP_DIR/a64_json.txt" "$TMP_DIR/a64_enum.txt")
enum_missing_from_json=$(comm -13 "$TMP_DIR/a64_json.txt" "$TMP_DIR/a64_enum.txt")

if [ -n "$json_missing_from_enum" ]; then
    echo "    WARNING: In JSON but missing from enum:"
    echo "$json_missing_from_enum" | sed 's/^/      - /'
    ERRORS=$((ERRORS + 1))
fi
if [ -n "$enum_missing_from_json" ]; then
    echo "    NOTE: In enum but not in JSON (pseudo-ops or not yet documented):"
    echo "$enum_missing_from_json" | sed 's/^/      - /'
fi
if [ $ERRORS -eq $PREV_ERRORS ]; then
    echo "    OK — JSON entries are a subset of enum"
fi

echo ""

# ============================================================
# Summary
# ============================================================
if [ $ERRORS -eq 0 ]; then
    echo "=== ALL CHECKS PASSED ==="
    exit 0
else
    echo "=== $ERRORS CHECK(S) FAILED ==="
    exit 1
fi
