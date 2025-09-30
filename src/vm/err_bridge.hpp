// File: src/vm/err_bridge.hpp
// Purpose: Defines temporary runtime error bridge mapping legacy codes to TrapKind.
// Key invariants: Mapping remains internal to the VM until runtime emits structured errors.
// Ownership/Lifetime: Header-only helpers; no dynamic state.
// Links: docs/il-guide.md#reference
#pragma once

#include "vm/Trap.hpp"

#include <cstdint>

#ifdef EOF
#pragma push_macro("EOF")
#undef EOF
#define IL_VM_ERR_BRIDGE_RESTORE_EOF 1
#endif

namespace il::vm
{

/// @brief Legacy runtime error codes forwarded through the VM bridge.
enum class ErrCode : int32_t
{
    Err_None = 0,
    Err_FileNotFound = 1,
    Err_EOF = 2,
    Err_IOError = 3,
    Err_Overflow = 4,
    Err_InvalidCast = 5,
    Err_DomainError = 6,
    Err_Bounds = 7,
    Err_InvalidOperation = 8,
    Err_RuntimeError = 9,
};

/// @brief Map a legacy runtime error code to the corresponding trap kind.
/// @param err_code Numeric code reported by the runtime.
/// @return TrapKind describing the semantic category of the error.
inline TrapKind map_err_to_trap(int err_code)
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

#ifdef IL_VM_ERR_BRIDGE_RESTORE_EOF
#pragma pop_macro("EOF")
#undef IL_VM_ERR_BRIDGE_RESTORE_EOF
#endif

} // namespace il::vm
