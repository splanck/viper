//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
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

/// @brief Store an array value into a slot while observing runtime ownership rules.
/// @details Delegates to the shared emitter so the borrow/retain conventions and
///          slot indexing rules remain consistent across the lowering pipeline.
///          The helper is intentionally thin but keeps callers oblivious to the
///          emitter implementation details.
/// @param slot Destination register or temporary slot that will receive the array.
/// @param value Array payload produced by the lowering code.
void Lowerer::storeArray(Value slot, Value value)
{
    emitter().storeArray(slot, value);
}

/// @brief Release array locals captured in the current procedure scope.
/// @details Forwards to the emitter so that any retained runtime handles are
///          released at deterministic control-flow boundaries.  The @p paramNames
///          set informs the emitter which locals overlap formal parameters so it
///          can avoid double releases.
/// @param paramNames Names of parameters that should be excluded from local release.
void Lowerer::releaseArrayLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayLocals(paramNames);
}

/// @brief Release retained array parameters on procedure exit.
/// @details Ensures that arrays passed by reference are relinquished when the
///          procedure finishes.  Responsibility is delegated to the shared
///          emitter so the sequence of runtime calls stays uniform across all
///          lowering entry points.
/// @param paramNames Formal parameter names whose array storage must be released.
void Lowerer::releaseArrayParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseArrayParams(paramNames);
}

} // namespace il::frontends::basic
