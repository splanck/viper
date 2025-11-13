//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares constant folding utilities for BASIC expressions,
// performing compile-time evaluation of constant expressions to improve
// generated IL quality.
//
// Constant Folding:
// Constant folding is an optimization pass that evaluates expressions with
// literal operands at compile time, replacing them with their computed results:
//   3 + 5      →  8
//   10 * 2 + 1 →  21
//   "Hello" + " " + "World" → "Hello World"
//
// This optimization:
// - Reduces IL instruction count
// - Enables better dead code elimination
// - Simplifies expressions for easier semantic analysis
// - Improves runtime performance by eliminating redundant computations
//
// Folding Rules:
// Only pure expressions with constant operands are folded:
// - Binary arithmetic: +, -, *, /, \, MOD, ^
// - Unary arithmetic: -, NOT
// - Comparison: =, <, >, <=, >=, <>
// - Logical: AND, OR, NOT
// - String concatenation: &
//
// Expressions involving:
// - Variables
// - Function calls (except certain pure intrinsics)
// - I/O operations
// are NOT folded since they may have side effects or depend on runtime state.
//
// AST Transformation:
// The folder mutates the AST in place, replacing:
//   BinaryExpr(IntLiteral(3), +, IntLiteral(5))
// with:
//   IntLiteral(8)
//
// Ownership:
// - Functions mutate AST in place
// - AST nodes remain owned by caller
// - Replaced nodes are properly deallocated
//
// Integration:
// - Called by: BasicCompiler after parsing and before semantic analysis
// - Operates on: Complete Program AST
// - Preserves: AST structure and semantics
//
// Design Notes:
// - Only folds pure expressions to preserve program semantics
// - Handles all BASIC literal types (integer, float, string, boolean)
// - Propagates type information through folded expressions
// - Safe for use before semantic analysis
//
//===----------------------------------------------------------------------===//
#pragma once

#include "frontends/basic/Token.hpp"
#include "frontends/basic/ast/DeclNodes.hpp"

namespace il::frontends::basic
{

/// \brief Fold constant expressions within a BASIC program AST.
/// \param prog Program to transform in place.
void foldConstants(Program &prog);

} // namespace il::frontends::basic
