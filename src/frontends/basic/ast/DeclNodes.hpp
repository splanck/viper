//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/ast/DeclNodes.hpp
// Purpose: Defines BASIC declaration aggregates composing higher-level program structure.
// Key invariants: Program partitions procedure declarations from main statements while
// Ownership/Lifetime: Nodes are owned via std::unique_ptr managed by callers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/StmtNodesAll.hpp"

#include <vector>

namespace il::frontends::basic
{
/// @brief Root node partitioning procedure declarations from main statements.
struct Program
{
    /// FUNCTION/SUB declarations in order.
    std::vector<ProcDecl> procs;

    /// Top-level statements forming program entry.
    std::vector<StmtPtr> main;

    /// Location of first token in source.
    il::support::SourceLoc loc{};
};

} // namespace il::frontends::basic
