// File: src/frontends/basic/AstWalkerUtils.cpp
// Purpose: Implements helper utilities shared by BASIC AST walker implementations.
// Key invariants: Utilities mirror BasicAstWalker traversal semantics.
// Ownership/Lifetime: Functions operate on borrowed nodes.
// Links: docs/codemap.md

#include "frontends/basic/AstWalkerUtils.hpp"

namespace il::frontends::basic::walker
{

bool printItemHasExpr(const PrintItem &item) noexcept
{
    return item.kind == PrintItem::Kind::Expr && item.expr != nullptr;
}

} // namespace il::frontends::basic::walker
