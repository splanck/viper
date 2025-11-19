//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerStmt_Control.cpp
// Purpose: Delegation layer for control flow statement lowering.
//          All implementations have been migrated to ControlStatementLowerer.
// Key invariants: Maintains backward compatibility by forwarding to ControlStatementLowerer
// Ownership/Lifetime: Functions delegate to ctrlStmtLowerer_ member
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/ControlStatementLowerer.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{

void Lowerer::lowerGosub(const GosubStmt &stmt)
{
    ctrlStmtLowerer_->lowerGosub(stmt);
}

void Lowerer::lowerGoto(const GotoStmt &stmt)
{
    ctrlStmtLowerer_->lowerGoto(stmt);
}

void Lowerer::lowerGosubReturn(const ReturnStmt &stmt)
{
    ctrlStmtLowerer_->lowerGosubReturn(stmt);
}

void Lowerer::lowerEnd(const EndStmt &stmt)
{
    ctrlStmtLowerer_->lowerEnd(stmt);
}

} // namespace il::frontends::basic
