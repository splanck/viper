// File: src/frontends/basic/LowerScan.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Orchestrates BASIC scan passes for expression types and runtime needs.
// Key invariants: Scan sets flags only; no IR emission.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md

#include "frontends/basic/Lowerer.hpp"

namespace il::frontends::basic
{
namespace scan_detail
{
Lowerer::ExprType scanExprType(Lowerer &, const Expr &);
void scanStmtExprTypes(Lowerer &, const Stmt &);
void scanProgramExprTypes(Lowerer &, const Program &);
void scanStmtRuntimeNeeds(Lowerer &, const Stmt &);
void scanProgramRuntimeNeeds(Lowerer &, const Program &);
} // namespace scan_detail

void Lowerer::scanStmt(const Stmt &stmt)
{
    // Scan sets flags only; no IR emission.
    scan_detail::scanStmtExprTypes(*this, stmt);
    scan_detail::scanStmtRuntimeNeeds(*this, stmt);
}

void Lowerer::scanProgram(const Program &prog)
{
    // Scan sets flags only; no IR emission.
    scan_detail::scanProgramExprTypes(*this, prog);
    scan_detail::scanProgramRuntimeNeeds(*this, prog);
}

} // namespace il::frontends::basic
