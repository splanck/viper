//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/err_bridge.cpp
// Purpose: Map legacy BASIC runtime error codes onto VM trap categories.
// Key invariants: Unknown codes degrade to TrapKind::RuntimeError so tooling
//                 always receives a defined classification.
// Ownership/Lifetime: Pure functions only; no persistent state.
// Links: docs/runtime-vm.md#traps
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Maps BASIC runtime error numbers onto VM trap categories.
/// @details The BASIC front end still emits historical error codes in a few
///          places.  This bridge keeps the mapping consolidated so the VM can
///          expose a modern trap classification without leaking old codes.

#include "vm/err_bridge.hpp"

#ifdef EOF
#pragma push_macro("EOF")
#undef EOF
#define IL_VM_ERR_BRIDGE_CPP_RESTORE_EOF 1
#endif

namespace il::vm
{
/// @brief Translate legacy BASIC error codes into @ref TrapKind enumerators.
///
/// @details Matches the historic runtime error numbers used by the BASIC
///          frontend to the structured trap categories consumed by the VM.
///          Unknown codes fall back to @ref TrapKind::RuntimeError to preserve
///          existing behaviour so tooling never encounters an unmapped trap.
///
/// @param err_code Numeric error code originating from the BASIC runtime.
/// @return Equivalent @ref TrapKind classification.
TrapKind map_err_to_trap(int err_code)
{
    switch (err_code)
    {
        case 1:
            return TrapKind::FileNotFound;
        case 2:
            return TrapKind::EOF;
        case 3:
            return TrapKind::IOError;
        case 4:
            return TrapKind::Overflow;
        case 5:
            return TrapKind::InvalidCast;
        case 6:
            return TrapKind::DomainError;
        case 7:
            return TrapKind::Bounds;
        case 8:
            return TrapKind::InvalidOperation;
        case 9:
            return TrapKind::RuntimeError;
        default:
            return TrapKind::RuntimeError;
    }
}
} // namespace il::vm

#ifdef IL_VM_ERR_BRIDGE_CPP_RESTORE_EOF
#pragma pop_macro("EOF")
#undef IL_VM_ERR_BRIDGE_CPP_RESTORE_EOF
#endif
