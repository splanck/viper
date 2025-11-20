//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/ControlStatementLowerer.hpp
// Purpose: Declares ControlStatementLowerer class that handles lowering of
//          BASIC control flow statements (GOSUB, GOTO, RETURN, END) to IL.
//          Extracted from Lowerer to demonstrate modular extraction pattern.
// Key invariants: Operates on Lowerer context; maintains deterministic block graphs
// Ownership/Lifetime: Borrows Lowerer reference; does not own AST or IR
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

class Lowerer;

/// @brief Handles lowering of BASIC control flow statements to IL branches.
/// @invariant All methods operate on the Lowerer's active context
/// @ownership Borrows Lowerer reference for state access and delegation
class ControlStatementLowerer
{
  public:
    /// @brief Construct a control statement lowerer bound to a Lowerer instance.
    /// @param lowerer Parent lowerer providing context and helper methods
    explicit ControlStatementLowerer(Lowerer &lowerer);

    /// @brief Lower GOSUB statement for subroutine calls.
    void lowerGosub(const GosubStmt &stmt);

    /// @brief Lower GOTO statement for unconditional jumps.
    void lowerGoto(const GotoStmt &stmt);

    /// @brief Lower RETURN statement from GOSUB.
    void lowerGosubReturn(const ReturnStmt &stmt);

    /// @brief Lower END statement to terminate program.
    void lowerEnd(const EndStmt &stmt);

    Lowerer &lowerer_; ///< Parent lowerer providing context and helpers
};

} // namespace il::frontends::basic
