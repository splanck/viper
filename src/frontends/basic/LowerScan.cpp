// File: src/frontends/basic/LowerScan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Thin orchestrator delegating BASIC scan passes to specialized helpers.
// Key invariants: Scan sets flags only; no IR emission.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

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

Lowerer::ExprType Lowerer::scanExpr(const Expr &expr)
{
    return lower::scanExprTypes(*this, expr);
}

Lowerer::ExprType Lowerer::scanBuiltinCallExpr(const BuiltinCallExpr &expr)
{
    return lower::scanBuiltinExprTypes(*this, expr);
}

void Lowerer::scanStmt(const Stmt &stmt)
{
    lower::scanStmtRuntimeNeeds(*this, stmt);
}

void Lowerer::scanProgram(const Program &prog)
{
    lower::scanProgramRuntimeNeeds(*this, prog);
}

} // namespace il::frontends::basic
