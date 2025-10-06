// File: src/vm/err_bridge.cpp
// Purpose: Translation unit for the temporary runtime error bridge helpers.
// Key invariants: Contains no state; exists to participate in the VM library build.
// Ownership/Lifetime: Not applicable.
// Links: docs/il-guide.md#reference

#include "vm/err_bridge.hpp"

#ifdef EOF
#pragma push_macro("EOF")
#undef EOF
#define IL_VM_ERR_BRIDGE_CPP_RESTORE_EOF 1
#endif

namespace il::vm
{
/// @copydoc map_err_to_trap
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
