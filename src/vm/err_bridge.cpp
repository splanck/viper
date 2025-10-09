//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Provides the runtime error bridge that converts legacy BASIC error codes into
// the VM's structured @ref TrapKind enumeration.  The helper is intentionally
// simple and stateless.
//
//===----------------------------------------------------------------------===//

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
/// Matches the historic runtime error numbers used by the BASIC frontend to the
/// structured trap categories consumed by the VM.  Unknown codes fall back to
/// @ref TrapKind::RuntimeError to preserve existing behaviour.
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
