#!/usr/bin/env bash
# check_vm_semantics_duplication.sh
#
# Fails when VM scalar semantics drift back into bytecode-local implementations.
# This is a structural guard that complements behavioral conformance tests.

set -euo pipefail

ROOT_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

fail() {
    printf 'check_vm_semantics_duplication: %s\n' "$*" >&2
    exit 1
}

require_include() {
    local file="$1"
    local include="$2"
    if ! grep -Fq "$include" "${ROOT_DIR}/${file}"; then
        fail "${file} must include ${include}"
    fi
}

reject_pattern() {
    local pattern="$1"
    shift
    local hits
    hits="$(mktemp "${TMPDIR:-/tmp}/zanna_semantics_duplication_hits.XXXXXX")"
    if command -v rg >/dev/null 2>&1; then
        rg -n --fixed-strings "$pattern" "$@" >"${hits}" || true
    else
        grep -R -n --fixed-strings "$pattern" "$@" >"${hits}" || true
    fi
    if [[ -s "${hits}" ]]; then
        cat "${hits}" >&2
        rm -f "${hits}"
        fail "forbidden bytecode-local scalar semantic primitive: ${pattern}"
    fi
    rm -f "${hits}"
}

BYTECODE_VM_FILES=(
    "${ROOT_DIR}/src/bytecode/BytecodeVM.cpp"
    "${ROOT_DIR}/src/bytecode/BytecodeVM_threaded.cpp"
)

require_include "src/bytecode/BytecodeVM.cpp" '#include "bytecode/BytecodeSemantics.hpp"'
require_include "src/bytecode/BytecodeVM_threaded.cpp" '#include "bytecode/BytecodeSemantics.hpp"'
require_include "src/bytecode/BytecodeCompiler.cpp" '#include "bytecode/BytecodeSemantics.hpp"'
require_include "src/vm/IntOpSupport.hpp" '#include "il/semantics/ScalarOps.hpp"'
require_include "src/vm/fp_ops.cpp" '#include "il/semantics/ScalarOps.hpp"'

reject_pattern "__builtin_add_overflow" "${BYTECODE_VM_FILES[@]}"
reject_pattern "__builtin_sub_overflow" "${BYTECODE_VM_FILES[@]}"
reject_pattern "__builtin_mul_overflow" "${BYTECODE_VM_FILES[@]}"
reject_pattern "std::rint" "${BYTECODE_VM_FILES[@]}"
reject_pattern "std::ldexp" "${BYTECODE_VM_FILES[@]}"
reject_pattern "idx < lo || idx >= hi" "${BYTECODE_VM_FILES[@]}"

printf 'check_vm_semantics_duplication: OK\n'
