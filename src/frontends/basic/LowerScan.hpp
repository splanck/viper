// File: src/frontends/basic/LowerScan.hpp
// Purpose: Declares AST scanning helpers for BASIC lowering.
// Key invariants: Scanning only mutates bookkeeping flags; no IR is emitted.
// Ownership/Lifetime: Operates on Lowerer state without owning AST or module.
// Links: docs/class-catalog.md
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

public:
ExprType scanBuiltinCallExpr(const BuiltinCallExpr &c);
ExprType scanLen(const BuiltinCallExpr &c);
ExprType scanMid(const BuiltinCallExpr &c);
ExprType scanLeft(const BuiltinCallExpr &c);
ExprType scanRight(const BuiltinCallExpr &c);
ExprType scanStr(const BuiltinCallExpr &c);
ExprType scanVal(const BuiltinCallExpr &c);
ExprType scanInt(const BuiltinCallExpr &c);
ExprType scanSqr(const BuiltinCallExpr &c);
ExprType scanAbs(const BuiltinCallExpr &c);
ExprType scanFloor(const BuiltinCallExpr &c);
ExprType scanCeil(const BuiltinCallExpr &c);
ExprType scanSin(const BuiltinCallExpr &c);
ExprType scanCos(const BuiltinCallExpr &c);
ExprType scanPow(const BuiltinCallExpr &c);
ExprType scanRnd(const BuiltinCallExpr &c);
ExprType scanInstr(const BuiltinCallExpr &c);
ExprType scanLtrim(const BuiltinCallExpr &c);
ExprType scanRtrim(const BuiltinCallExpr &c);
ExprType scanTrim(const BuiltinCallExpr &c);
ExprType scanUcase(const BuiltinCallExpr &c);
ExprType scanLcase(const BuiltinCallExpr &c);
ExprType scanChr(const BuiltinCallExpr &c);
ExprType scanAsc(const BuiltinCallExpr &c);

private:
void scanStmt(const Stmt &s);
/// @brief Analyze @p prog for runtime usage prior to emission.
void scanProgram(const Program &prog);
