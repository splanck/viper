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
//
// What BASIC syntax it handles:
//   - Terminal control: BEEP, CLS, COLOR, LOCATE, CURSOR, ALTSCREEN, SLEEP
//   - Variable assignment: LET var = expr, arr(i) = expr, obj.field = expr
//   - Variable declarations: DIM, REDIM, CONST, STATIC
//   - Miscellaneous: RANDOMIZE, SWAP
//
// Invariants expected from Lowerer/LoweringContext:
//   - Active procedure context must have a valid function and current block
//   - Symbol table must be populated with variable/parameter metadata
//   - Runtime feature tracker must be available for helper requests
//
// IL Builder interaction:
//   - Emits runtime helper calls (rt_bell, rt_term_*, rt_arr_*, etc.)
//   - Uses Lowerer's emit* methods for instructions and control flow
//   - Creates auxiliary basic blocks for bounds checking
//
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
///
/// @details This class encapsulates the lowering logic for BASIC statements that
///          require runtime library support rather than pure IL instruction generation.
///          It handles terminal control statements (BEEP, CLS, COLOR, etc.), variable
///          assignment (including array element and object field assignment), and
///          variable declaration statements (DIM, REDIM, CONST, STATIC).
///
/// The class demonstrates the modular extraction pattern used in the BASIC frontend:
/// complex lowering concerns are factored into dedicated helper classes that
/// coordinate with the main Lowerer through its public/friend interface.
///
/// @invariant All methods operate on the Lowerer's active procedure context
/// @invariant The lowerer's current block is valid and not terminated before calls
/// @ownership Borrows Lowerer reference; does not own AST, IR, or runtime state
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
