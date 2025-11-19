//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerStmt_Runtime.cpp
// Purpose: Delegation layer for runtime statement lowering.
//          All implementations have been migrated to RuntimeStatementLowerer.
// Key invariants: Maintains backward compatibility by forwarding to RuntimeStatementLowerer
// Ownership/Lifetime: Functions delegate to runtimeStmtLowerer_ member
// Links: docs/codemap.md, docs/architecture.md#cpp-overview
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"
#include "frontends/basic/RuntimeStatementLowerer.hpp"

namespace il::frontends::basic
{

// Terminal control statements
void Lowerer::visit(const BeepStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

void Lowerer::visit(const ClsStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

void Lowerer::visit(const ColorStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

void Lowerer::visit(const LocateStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

void Lowerer::visit(const CursorStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

void Lowerer::visit(const AltScreenStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

void Lowerer::visit(const SleepStmt &s)
{
    runtimeStmtLowerer_->visit(s);
}

// Assignment operations
void Lowerer::assignScalarSlot(const SlotType &slotInfo,
                               Value slot,
                               RVal value,
                               il::support::SourceLoc loc)
{
    runtimeStmtLowerer_->assignScalarSlot(slotInfo, slot, value, loc);
}

void Lowerer::assignArrayElement(const ArrayExpr &target, RVal value, il::support::SourceLoc loc)
{
    runtimeStmtLowerer_->assignArrayElement(target, value, loc);
}

void Lowerer::lowerLet(const LetStmt &stmt)
{
    runtimeStmtLowerer_->lowerLet(stmt);
}

// Variable declarations
void Lowerer::lowerConst(const ConstStmt &stmt)
{
    runtimeStmtLowerer_->lowerConst(stmt);
}

void Lowerer::lowerStatic(const StaticStmt &stmt)
{
    runtimeStmtLowerer_->lowerStatic(stmt);
}

void Lowerer::lowerDim(const DimStmt &stmt)
{
    runtimeStmtLowerer_->lowerDim(stmt);
}

void Lowerer::lowerReDim(const ReDimStmt &stmt)
{
    runtimeStmtLowerer_->lowerReDim(stmt);
}

// Miscellaneous runtime statements
void Lowerer::lowerRandomize(const RandomizeStmt &stmt)
{
    runtimeStmtLowerer_->lowerRandomize(stmt);
}

void Lowerer::lowerSwap(const SwapStmt &stmt)
{
    runtimeStmtLowerer_->lowerSwap(stmt);
}

// Helper method
Lowerer::Value Lowerer::emitArrayLengthCheck(Value bound,
                                             il::support::SourceLoc loc,
                                             std::string_view labelBase)
{
    return runtimeStmtLowerer_->emitArrayLengthCheck(bound, loc, labelBase);
}

} // namespace il::frontends::basic
