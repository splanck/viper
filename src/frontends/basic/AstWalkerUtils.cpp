//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/AstWalkerUtils.cpp
// Purpose: Implements helper utilities shared by BASIC AST walker implementations.
// Key invariants: Utilities mirror BasicAstWalker traversal semantics.
// Ownership/Lifetime: Functions operate on borrowed nodes.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/AstWalkerUtils.hpp"

namespace il::frontends::basic::walker
{

/// @brief Check whether a PRINT item contains a dynamic expression.
/// @details BASIC PRINT statements can mix literals and evaluated expressions.
///          The helper inspects the discriminated union describing an item and
///          returns @c true only when the node represents an expression with a
///          non-null pointer.  Callers use this check before triggering
///          expression lowering so literal-only prints can fast-path to
///          constant emission.
/// @param item PRINT item under inspection.
/// @return @c true when @p item holds an expression node; otherwise @c false.
bool printItemHasExpr(const PrintItem &item) noexcept
{
    return item.kind == PrintItem::Kind::Expr && item.expr != nullptr;
}

} // namespace il::frontends::basic::walker
