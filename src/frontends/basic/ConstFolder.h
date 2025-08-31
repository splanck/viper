// File: src/frontends/basic/ConstFolder.h
// Purpose: Declares utilities to fold constant BASIC expressions.
// Key invariants: Only pure expressions with literal operands are folded.
// Ownership/Lifetime: Functions mutate AST in place, nodes owned by caller.
// Links: docs/class-catalog.md
#pragma once
#include "frontends/basic/AST.h"

namespace il::frontends::basic {

/// \brief Fold constant expressions within a BASIC program AST.
/// \param prog Program to transform in place.
void foldConstants(Program &prog);

} // namespace il::frontends::basic
