//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/lower/Lower_Switch.cpp
// Purpose: Lower BASIC SELECT CASE constructs by delegating to the specialised
//          SelectCaseLowering helper while synchronising control-flow state.
// Key invariants: Control state returned by the helper reflects the Lowerer's
//                 current block and fallthrough semantics after lowering.
// Ownership/Lifetime: Functions operate on the calling @ref Lowerer and do not
//                     take ownership of IL blocks.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements SELECT CASE lowering helpers for the BASIC front end.
/// @details Delegates the heavy lifting to SelectCaseLowering while ensuring the
/// resulting control-flow state is reflected back into the owning Lowerer.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SelectCaseLowering.hpp"

namespace il::frontends::basic
{

/// @brief Lower a SELECT CASE statement using the SelectCaseLowering utility.
///
/// @details Constructs a helper instance bound to this @ref Lowerer, invokes it
///          to produce the control-flow graph, and then packages the resulting
///          state into a @ref CtrlState.  The helper keeps the lowerer's current
///          block in sync so subsequent lowering steps see the correct CFG.
/// @param stmt AST node describing the SELECT CASE statement.
/// @return Control-flow state capturing the block and fallthrough outcome.
Lowerer::CtrlState Lowerer::emitSelect(const SelectCaseStmt &stmt)
{
    CtrlState state{};
    SelectCaseLowering lowering(*this);
    lowering.lower(stmt);
    state.cur = context().current();
    state.after = state.cur;
    state.fallthrough = state.cur && !state.cur->terminated;
    return state;
}

} // namespace il::frontends::basic
