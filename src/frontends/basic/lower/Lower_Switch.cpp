//===----------------------------------------------------------------------===//
// MIT License. See LICENSE file in the project root for full text.
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements SELECT CASE lowering helpers for the BASIC front end.
/// @details Delegates the heavy lifting to SelectCaseLowering while ensuring the
/// resulting control-flow state is reflected back into the owning Lowerer.

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/SelectCaseLowering.hpp"

namespace il::frontends::basic
{

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
