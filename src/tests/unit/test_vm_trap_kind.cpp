//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_vm_trap_kind.cpp
// Purpose: Validate TrapKind helpers provide stable string names and decoding. 
// Key invariants: Bidirectional mapping covers every TrapKind enumerator and fallback path.
// Ownership/Lifetime: Standalone unit test executable.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "vm/Trap.hpp"

#include <array>
#include <cassert>
#include <string_view>

#ifdef EOF
#pragma push_macro("EOF")
#undef EOF
#define IL_VM_TRAP_KIND_TEST_RESTORE_EOF 1
#endif

using il::vm::TrapKind;

int main()
{
    constexpr std::array<std::pair<TrapKind, std::string_view>, 10> cases{{
        {TrapKind::DivideByZero, "DivideByZero"},
        {TrapKind::Overflow, "Overflow"},
        {TrapKind::InvalidCast, "InvalidCast"},
        {TrapKind::DomainError, "DomainError"},
        {TrapKind::Bounds, "Bounds"},
        {TrapKind::FileNotFound, "FileNotFound"},
        {TrapKind::EOF, "EOF"},
        {TrapKind::IOError, "IOError"},
        {TrapKind::InvalidOperation, "InvalidOperation"},
        {TrapKind::RuntimeError, "RuntimeError"},
    }};

    for (const auto &[kind, name] : cases)
    {
        const auto stringified = il::vm::toString(kind);
        assert(stringified == name);

        const auto decoded = il::vm::trapKindFromValue(static_cast<int32_t>(kind));
        assert(decoded == kind);
    }

    const auto fallbackKind = il::vm::trapKindFromValue(127);
    assert(fallbackKind == TrapKind::RuntimeError);

    const auto fallbackName = il::vm::toString(static_cast<TrapKind>(-42));
    assert(fallbackName == std::string_view("RuntimeError"));

    return 0;
}

#ifdef IL_VM_TRAP_KIND_TEST_RESTORE_EOF
#pragma pop_macro("EOF")
#undef IL_VM_TRAP_KIND_TEST_RESTORE_EOF
#endif
