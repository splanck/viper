//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SemanticAnalyzer.Stmts.Control.hpp
// Purpose: Declares helpers specific to control-flow statement analysis for the
// Key invariants: Helpers reuse shared RAII guards to keep loop tracking
// Ownership/Lifetime: Non-owning views over SemanticAnalyzer state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/SemanticAnalyzer.Stmts.Shared.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Context wrapper for control-flow statement analysis helpers.
class ControlStmtContext : public StmtShared
{
  public:
    using LoopGuard = StmtShared::LoopGuard;
    using ForLoopGuard = StmtShared::ForLoopGuard;

    explicit ControlStmtContext(SemanticAnalyzer &analyzer) noexcept : StmtShared(analyzer) {}
};

} // namespace il::frontends::basic::semantic_analyzer_detail
