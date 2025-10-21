//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// File: src/frontends/basic/lower/Emit_OOP.cpp
// Purpose: Emit ownership management helpers for BASIC object-oriented features.
// Invariants:
//   * Release sites execute before control leaves a block so runtime reference
//     counts remain balanced.
//   * ProcedureContext bookkeeping determines which temporaries are safe to
//     release without affecting caller-owned instances.
// Ownership: Functions borrow Lowerer state and never retain procedure-local
//            handles beyond the call boundary.
// Links: docs/codemap.md
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements object ownership emission for BASIC OOP features.
/// @details Helpers release retained object handles at scope exits. They
///          consult the @ref Lowerer::ProcedureContext for active blocks and append
///          control-flow as required, leaving terminator management consistent
///          with control helpers. Temporary values remain owned by the lowerer
///          while runtime helpers manage the underlying reference counts.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/lower/Emitter.hpp"
#include "frontends/basic/NameMangler_OOP.hpp"

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"

#include <cassert>

using namespace il::core;

namespace il::frontends::basic
{

/// @brief Release object handles captured in local variables.
///
/// @details Delegates to the emitter so it can walk the @ref Lowerer::ProcedureContext and
///          emit retain/release pairs for each tracked object slot.  The
///          @p paramNames set marks parameters that should remain borrowed,
///          preventing the helper from releasing caller-owned instances.
///
/// @param paramNames Parameter names to exclude from release.
void Lowerer::releaseObjectLocals(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseObjectLocals(paramNames);
}

/// @brief Release object parameters at the end of a procedure.
///
/// @details Called from epilogue emission to balance retains introduced when
///          parameters were passed by reference.  The emitter emits runtime
///          calls only when the object lowering pass flagged the associated
///          features as required, ensuring no unnecessary helpers execute.
///
/// @param paramNames Names of parameters that carry borrowed object handles.
void Lowerer::releaseObjectParams(const std::unordered_set<std::string> &paramNames)
{
    emitter().releaseObjectParams(paramNames);
}

} // namespace il::frontends::basic
