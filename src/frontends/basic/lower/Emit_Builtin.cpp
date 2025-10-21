//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Emit_Builtin.cpp
// Purpose: Emit IL helpers that bridge BASIC built-ins to runtime entry points.
// Invariants:
//   * Helper routines never insert terminators; control helpers remain
//     responsible for finalising blocks.
//   * Array ownership flows follow the ProcedureContext bookkeeping so borrowed
//     temporaries are released exactly once.
// Ownership: Functions borrow Lowerer state and do not persist references past
//            the duration of the call.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements runtime helper emission for BASIC built-ins.
/// @details These utilities manage array ownership by retaining and releasing
///          handles while appending the required calls to the active block. They
///          do not produce terminators and therefore rely on control helpers to
///          manage block lifetimes; temporaries remain owned by the lowerer and
///          follow the standard ProcedureContext tracking.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Instr.hpp"

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Persist a lowered array value into the destination slot.
///
/// @details Delegates to @ref il::frontends::basic::lower::Emitter so the
///          centralised helper can coordinate reference counting and write
///          barriers.  Callers invoke
///          this immediately after producing a value to ensure the runtime sees
///          consistent ownership semantics before any further mutations occur.
///
/// @param slot Destination slot that receives the stored array handle.
/// @param value Evaluated array value to store.
void Lowerer::storeArray(Value slot, Value value)
{
    emitter().storeArray(slot, value);
}

/// @brief Release any temporary array handles owned by local variables.
///
/// @details The helper forwards to the emitter which inspects the active
///          @ref Lowerer::ProcedureContext and emits release calls for each tracked
///          array.  The @p paramNames set identifies parameters that must remain
///          alive, preventing accidental double releases.
///
/// @param paramNames Procedure parameters that should be excluded from release.
void Lowerer::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayLocals(paramNames);
}

/// @brief Release array parameters once a procedure frame is leaving scope.
///
/// @details Invoked during epilogue emission so borrowed array handles are
///          returned to the runtime.  The emitter handles deduplicating release
///          sites and emits calls only when the runtime requested reference
///          counting support for arrays.
///
/// @param paramNames Names of formal parameters that were passed by reference.
void Lowerer::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayParams(paramNames);
}

} // namespace il::frontends::basic
