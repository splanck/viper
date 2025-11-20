// File: src/frontends/basic/SemanticAnalyzer.Stmts.Runtime.hpp
// License: GPL-3.0-only. See LICENSE in the project root for full license
//          information.
// Purpose: Declares helpers specific to runtime/data-manipulation statement
//          analysis for the BASIC semantic analyzer.
// Key invariants: Runtime helpers mirror the analyzer's internal state while
//                 reusing cross-cutting utilities.
// Ownership/Lifetime: Helpers borrow SemanticAnalyzer state.
// Links: docs/codemap.md
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
