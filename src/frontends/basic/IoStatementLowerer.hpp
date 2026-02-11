//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/IoStatementLowerer.hpp
// Purpose: Declares IoStatementLowerer class that handles lowering of BASIC
//          I/O statements (PRINT, INPUT, OPEN, CLOSE, SEEK, etc.) to IL.
//          Extracted from Lowerer to demonstrate modular extraction pattern.
// Key invariants: Operates on Lowerer context; delegates to runtime I/O functions
// Ownership/Lifetime: Borrows Lowerer reference; does not own AST or IR
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/AST.hpp"

namespace il::frontends::basic
{

class Lowerer;

/// @brief Handles lowering of BASIC I/O statements to IL runtime calls.
/// @invariant All methods operate on the Lowerer's active context
/// @ownership Borrows Lowerer reference for state access and delegation
class IoStatementLowerer
{
  public:
    /// @brief Construct an I/O statement lowerer bound to a Lowerer instance.
    /// @param lowerer Parent lowerer providing context and helper methods
    explicit IoStatementLowerer(Lowerer &lowerer);

    /// @brief Lower OPEN statement for file I/O.
    /// @param stmt OPEN statement AST node containing filename, mode, and channel.
    void lowerOpen(const OpenStmt &stmt);

    /// @brief Lower CLOSE statement to close file channels.
    /// @param stmt CLOSE statement AST node containing the channel to close.
    void lowerClose(const CloseStmt &stmt);

    /// @brief Lower SEEK statement for file positioning.
    /// @param stmt SEEK statement AST node containing channel and byte offset.
    void lowerSeek(const SeekStmt &stmt);

    /// @brief Lower PRINT statement for console output.
    /// @param stmt PRINT statement AST node containing the expression list to print.
    void lowerPrint(const PrintStmt &stmt);

    /// @brief Lower PRINT# statement for file output.
    /// @param stmt PRINT# statement AST node containing channel and expression list.
    void lowerPrintCh(const PrintChStmt &stmt);

    /// @brief Lower INPUT statement for console input.
    /// @param stmt INPUT statement AST node containing variable targets and optional prompt.
    void lowerInput(const InputStmt &stmt);

    /// @brief Lower INPUT# statement for file input.
    /// @param stmt INPUT# statement AST node containing channel and variable targets.
    void lowerInputCh(const InputChStmt &stmt);

    /// @brief Lower LINE INPUT# statement for line-based file input.
    /// @param stmt LINE INPUT# statement AST node containing channel and target variable.
    void lowerLineInputCh(const LineInputChStmt &stmt);

    Lowerer
        &lowerer_; ///< Parent lowerer providing context and helpers (public for file-local helpers)
};

} // namespace il::frontends::basic
