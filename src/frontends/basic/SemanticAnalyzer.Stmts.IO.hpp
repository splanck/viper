// File: src/frontends/basic/SemanticAnalyzer.Stmts.IO.hpp
// License: GPL-3.0-only. See LICENSE in the project root for full license
//          information.
// Purpose: Declares helpers specific to IO-oriented statement analysis for the
//          BASIC semantic analyzer.
// Key invariants: IO helpers leverage shared utilities without mutating public
//                 analyzer interface.
// Ownership/Lifetime: Helpers borrow SemanticAnalyzer state.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/SemanticAnalyzer.Stmts.Shared.hpp"

namespace il::frontends::basic::semantic_analyzer_detail
{

/// @brief Context wrapper for IO statement semantic helpers.
class IOStmtContext : public StmtShared
{
  public:
    using LoopGuard = StmtShared::LoopGuard;
    using ForLoopGuard = StmtShared::ForLoopGuard;
    explicit IOStmtContext(SemanticAnalyzer &analyzer) noexcept;
};

} // namespace il::frontends::basic::semantic_analyzer_detail
