// File: src/frontends/basic/ConstFolder.hpp
// Purpose: Declares utilities to fold constant BASIC expressions.
// Key invariants: Only pure expressions with literal operands are folded.
// Ownership/Lifetime: Functions mutate AST in place, nodes owned by caller.
// Links: docs/codemap.md
#pragma once

#include "frontends/basic/Token.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"
#include "frontends/basic/ast/StmtNodesAll.hpp"

namespace il::frontends::basic
{

/// \brief Fold constant expressions within a BASIC program AST.
/// \param prog Program to transform in place.
void foldConstants(Program &prog);

} // namespace il::frontends::basic
