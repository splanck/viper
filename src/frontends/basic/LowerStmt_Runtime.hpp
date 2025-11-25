//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/LowerStmt_Runtime.hpp
// Purpose: Declares runtime-oriented statement lowering helpers, including data
// Key invariants: Helpers interact with runtime support through the active
// Ownership/Lifetime: Declarations extend the Lowerer private interface for
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

void lowerLet(const LetStmt &stmt);
void lowerConst(const ConstStmt &stmt);
void lowerStatic(const StaticStmt &stmt);
void assignScalarSlot(const SlotType &slotInfo, Value slot, RVal value, il::support::SourceLoc loc);
void assignArrayElement(const ArrayExpr &target, RVal value, il::support::SourceLoc loc);
void lowerDim(const DimStmt &stmt);
void lowerReDim(const ReDimStmt &stmt);
void lowerRandomize(const RandomizeStmt &stmt);
void lowerSwap(const SwapStmt &stmt);
void visit(const BeepStmt &stmt);
void visit(const ClsStmt &stmt);
void visit(const ColorStmt &stmt);
void visit(const SleepStmt &stmt);
void visit(const LocateStmt &stmt);
void visit(const CursorStmt &stmt);
void visit(const AltScreenStmt &stmt);
Value emitArrayLengthCheck(Value bound, il::support::SourceLoc loc, std::string_view labelBase);
