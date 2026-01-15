//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file AST_Fwd.hpp
/// @brief Forward declarations and shared aliases for Zia AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "support/diagnostics.hpp"
#include <memory>

namespace il::frontends::zia
{
//===----------------------------------------------------------------------===//
/// @name Forward Declarations
/// @brief Forward declarations for AST node types and smart pointer aliases.
/// @details These enable circular references between node types (e.g., an
/// expression containing a block that contains statements).
/// @{
//===----------------------------------------------------------------------===//

struct Expr;
struct Stmt;
struct TypeNode;
struct Decl;

/// @brief Unique pointer to an expression node.
/// @details Expressions compute values and can be nested arbitrarily deep.
using ExprPtr = std::unique_ptr<Expr>;

/// @brief Unique pointer to a statement node.
/// @details Statements perform actions and may contain expressions.
using StmtPtr = std::unique_ptr<Stmt>;

/// @brief Unique pointer to a type annotation node.
/// @details Type nodes appear in variable declarations, function signatures,
/// and type casts. They represent syntactic type annotations, not resolved
/// semantic types (see Types.hpp for semantic types).
using TypePtr = std::unique_ptr<TypeNode>;

/// @brief Unique pointer to a declaration node.
/// @details Declarations introduce named entities: types, functions, fields.
using DeclPtr = std::unique_ptr<Decl>;

/// @}

//===----------------------------------------------------------------------===//
/// @name Source Location
/// @brief Type alias for source location tracking.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Source location for error messages and debugging.
/// @details Each AST node stores its source location to enable accurate
/// error reporting and source mapping during lowering.
using SourceLoc = il::support::SourceLoc;

/// @}

} // namespace il::frontends::zia
