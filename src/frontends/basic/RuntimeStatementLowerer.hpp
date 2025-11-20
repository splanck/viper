//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/RuntimeStatementLowerer.hpp
// Purpose: Declares RuntimeStatementLowerer class that handles lowering of
//          BASIC runtime statements (terminal control, assignments, variable
//          declarations, etc.) to IL.
//          Extracted from Lowerer to demonstrate modular extraction pattern.
// Key invariants: Operates on Lowerer context; manages runtime feature requests
// Ownership/Lifetime: Borrows Lowerer reference; does not own AST or IR
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AST.hpp"
#include "frontends/basic/Lowerer.hpp"

namespace il::support
{
struct SourceLoc;
}

namespace il::frontends::basic
{

/// @brief Handles lowering of BASIC runtime statements to IL runtime calls.
/// @invariant All methods operate on the Lowerer's active context
/// @ownership Borrows Lowerer reference for state access and delegation
class RuntimeStatementLowerer
{
  public:
    /// @brief Construct a runtime statement lowerer bound to a Lowerer instance.
    /// @param lowerer Parent lowerer providing context and helper methods
    explicit RuntimeStatementLowerer(Lowerer &lowerer);

    // Terminal control statements
    void visit(const BeepStmt &s);
    void visit(const ClsStmt &s);
    void visit(const ColorStmt &s);
    void visit(const LocateStmt &s);
    void visit(const CursorStmt &s);
    void visit(const AltScreenStmt &s);
    void visit(const SleepStmt &s);

    // Assignment operations
    void assignScalarSlot(const Lowerer::SlotType &slotInfo,
                          Lowerer::Value slot,
                          Lowerer::RVal value,
                          il::support::SourceLoc loc);
    void assignArrayElement(const ArrayExpr &target,
                            Lowerer::RVal value,
                            il::support::SourceLoc loc);
    void lowerLet(const LetStmt &stmt);

    // Variable declarations
    void lowerConst(const ConstStmt &stmt);
    void lowerStatic(const StaticStmt &stmt);
    void lowerDim(const DimStmt &stmt);
    void lowerReDim(const ReDimStmt &stmt);

    // Miscellaneous runtime statements
    void lowerRandomize(const RandomizeStmt &stmt);
    void lowerSwap(const SwapStmt &stmt);

    // Helper methods
    Lowerer::Value emitArrayLengthCheck(Lowerer::Value bound,
                                        il::support::SourceLoc loc,
                                        std::string_view labelBase);

    Lowerer &lowerer_; ///< Parent lowerer providing context and helpers
};

} // namespace il::frontends::basic
