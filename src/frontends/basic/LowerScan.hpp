// File: src/frontends/basic/LowerScan.hpp
// Purpose: Declares AST scanning helpers for BASIC lowering.
// Key invariants: Scanning only mutates bookkeeping flags; no IR is emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/codemap.md
#pragma once

public:
enum class ExprType
{
    I64,
    F64,
    Str,
    Bool,
};

private:
ExprType scanExpr(const Expr &e);
ExprType scanUnaryExpr(const UnaryExpr &u);
ExprType scanBinaryExpr(const BinaryExpr &b);
ExprType scanArrayExpr(const ArrayExpr &arr);
ExprType scanLBoundExpr(const LBoundExpr &expr);
ExprType scanUBoundExpr(const UBoundExpr &expr);

public:
ExprType scanBuiltinCallExpr(const BuiltinCallExpr &c);

private:
void scanStmt(const Stmt &s);
/// @brief Analyze @p prog for runtime usage prior to emission.
void scanProgram(const Program &prog);
