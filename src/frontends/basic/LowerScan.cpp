//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/LowerScan.cpp
// Purpose: Thin orchestrator delegating BASIC scan passes to specialized helpers.
// Key invariants: Scan sets flags only; no IR emission.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{
namespace lower
{
Lowerer::ExprType scanExprTypes(Lowerer &lowerer, const Expr &expr);
Lowerer::ExprType scanBuiltinExprTypes(Lowerer &lowerer, const BuiltinCallExpr &expr);
void scanStmtRuntimeNeeds(Lowerer &lowerer, const Stmt &stmt);
void scanProgramRuntimeNeeds(Lowerer &lowerer, const Program &prog);
} // namespace lower

/// @brief Classify the resulting type of an arbitrary BASIC expression.
/// @details Dispatches into the specialised scanning helpers that analyse the
///          AST without emitting IL.  The scan pass records any runtime feature
///          requirements while determining the expression type so later
///          lowering stages can coerce results correctly.
/// @param expr Expression to inspect.
/// @return Semantic type classification determined by the scan pass.
Lowerer::ExprType Lowerer::scanExpr(const Expr &expr)
{
    return lower::scanExprTypes(*this, expr);
}

/// @brief Determine the result type for a builtin-call expression.
/// @details Invokes the builtin-specific scan helper to evaluate argument
///          requirements and to record runtime feature usage without emitting
///          code.  This keeps builtin coercion rules isolated while still
///          surfacing type information to the caller.
/// @param expr Builtin call AST node under inspection.
/// @return Semantic type classification produced by the builtin analysis.
Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &expr)
{
    return lower::scanBuiltinExprTypes(*this, expr);
}

/// @brief Collect runtime requirements referenced by a statement.
/// @details Routes the statement through the runtime-needs scanner so that
///          later lowering stages already know which runtime helpers to declare
///          or retain.  The scan is side-effect free with respect to IR
///          emission and only updates bookkeeping inside the lowerer.
/// @param stmt Statement node to analyse.
void Lowerer::scanStmt(const Stmt &stmt)
{
    lower::scanStmtRuntimeNeeds(*this, stmt);
}

/// @brief Evaluate runtime feature usage for an entire program.
/// @details Scans every top-level statement and procedure so the lowerer can
///          predeclare runtime helpers before emission begins.  The method
///          resets no state beyond what the delegated scanners touch, allowing
///          callers to compose it with other preparatory passes.
/// @param prog Parsed BASIC program to scan.
void Lowerer::scanProgram(const Program &prog)
{
    lower::scanProgramRuntimeNeeds(*this, prog);
}

} // namespace il::frontends::basic
