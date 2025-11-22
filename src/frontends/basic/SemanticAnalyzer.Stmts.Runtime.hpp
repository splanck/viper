//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/SemanticAnalyzer.Stmts.Runtime.hpp
// Purpose: Declares helpers specific to runtime/data-manipulation statement 
// Key invariants: Runtime helpers mirror the analyzer's internal state while
// Ownership/Lifetime: Helpers borrow SemanticAnalyzer state.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/SemanticAnalyzer.Stmts.Shared.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Context wrapper for runtime statement semantic helpers.
class RuntimeStmtContext : public StmtShared
{
  public:
    using LoopGuard = StmtShared::LoopGuard;
    using ForLoopGuard = StmtShared::ForLoopGuard;
    explicit RuntimeStmtContext(SemanticAnalyzer &analyzer) noexcept;
};

} // namespace il::frontends::basic::semantic_analyzer_detail
