//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/basic/passes/CollectProcs.hpp
// Purpose: Post-parse pass to collect procedures declared inside namespaces and
// Key invariants: Qualified names are lowercase ASCII with dot separators.
// Ownership/Lifetime: Mutates the parsed AST in-place; no ownership transfers.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "frontends/basic/ast/DeclNodes.hpp"

namespace il::frontends::basic
{

/// @brief Walk the AST and assign qualified names to procedures inside namespaces.
/// @details DFS traverses `prog.main`, maintaining a namespace stack from
///          `NamespaceDecl` nodes. For each FunctionDecl/SubDecl encountered,
///          sets `namespacePath` and `qualifiedName` using canonicalized
///          lowercase segments. Top-level procedures remain unchanged.
///          Registration into semantic tables happens in later phases.
/// @param prog Parsed program to annotate in-place.
void CollectProcedures(Program &prog);

} // namespace il::frontends::basic
