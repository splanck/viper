// File: src/frontends/basic/LowerExprLogical.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements logical expression lowering helpers for the BASIC Lowerer.
// Key invariants: Logical operators preserve BASIC truthiness semantics using
//                 Lowerer utilities for short-circuiting.
// Ownership/Lifetime: Helpers borrow the Lowerer for the duration of the call.
// Links: docs/codemap.md

#include "frontends/basic/LowerExprLogical.hpp"

#include "frontends/basic/DiagnosticEmitter.hpp"

namespace il::frontends::basic
{
using namespace il::core;

using IlType = il::core::Type;
using IlKind = IlType::Kind;

namespace
{
std::string_view logicalOperatorDisplayName(BinaryExpr::Op op) noexcept
{
    switch (op)
    {
        case BinaryExpr::Op::LogicalAndShort:
            return "ANDALSO";
        case BinaryExpr::Op::LogicalOrShort:
            return "ORELSE";
        case BinaryExpr::Op::LogicalAnd:
            return "AND";
        case BinaryExpr::Op::LogicalOr:
            return "OR";
        default:
            break;
    }
    return "<logical>";
}

constexpr std::string_view kDiagUnsupportedLogicalOperator = "B4002";
} // namespace

LogicalExprLowering::LogicalExprLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

Lowerer::RVal LogicalExprLowering::lower(const BinaryExpr &expr)
{
    Lowerer &lowerer = *lowerer_;
    Lowerer::RVal lhs = lowerer.lowerExpr(*expr.lhs);
    lowerer.curLoc = expr.loc;

    auto toBool = [&](Lowerer::RVal val)
    { return lowerer.coerceToBool(std::move(val), expr.loc).value; };

    if (expr.op == BinaryExpr::Op::LogicalAndShort)
    {
        Value cond = toBool(lhs);
        Lowerer::RVal andResult = lowerer.lowerBoolBranchExpr(
            cond,
            expr.loc,
            [&](Value slot)
            {
                Lowerer::RVal rhs = lowerer.lowerExpr(*expr.rhs);
                Value rhsBool = toBool(std::move(rhs));
                lowerer.curLoc = expr.loc;
                lowerer.emitStore(lowerer.ilBoolTy(), slot, rhsBool);
            },
            [&](Value slot)
            {
                lowerer.curLoc = expr.loc;
                lowerer.emitStore(lowerer.ilBoolTy(), slot, lowerer.emitBoolConst(false));
            },
            "and_rhs",
            "and_false",
            "and_done");

        lowerer.curLoc = expr.loc;
        Value logical = lowerer.emitBasicLogicalI64(andResult.value);
        return {logical, IlType(IlKind::I64)};
    }

    if (expr.op == BinaryExpr::Op::LogicalOrShort)
    {
        Value cond = toBool(lhs);
        Lowerer::RVal orResult = lowerer.lowerBoolBranchExpr(
            cond,
            expr.loc,
            [&](Value slot)
            {
                lowerer.curLoc = expr.loc;
                lowerer.emitStore(lowerer.ilBoolTy(), slot, lowerer.emitBoolConst(true));
            },
            [&](Value slot)
            {
                Lowerer::RVal rhs = lowerer.lowerExpr(*expr.rhs);
                Value rhsBool = toBool(std::move(rhs));
                lowerer.curLoc = expr.loc;
                lowerer.emitStore(lowerer.ilBoolTy(), slot, rhsBool);
            },
            "or_true",
            "or_rhs",
            "or_done");

        lowerer.curLoc = expr.loc;
        Value logical = lowerer.emitBasicLogicalI64(orResult.value);
        return {logical, IlType(IlKind::I64)};
    }

    if (expr.op == BinaryExpr::Op::LogicalAnd)
    {
        Value lhsBool = toBool(lhs);
        Lowerer::RVal rhs = lowerer.lowerExpr(*expr.rhs);
        Value rhsBool = toBool(std::move(rhs));
        lowerer.curLoc = expr.loc;
        Value lhsLogical = lowerer.emitBasicLogicalI64(lhsBool);
        lowerer.curLoc = expr.loc;
        Value rhsLogical = lowerer.emitBasicLogicalI64(rhsBool);
        lowerer.curLoc = expr.loc;
        Value res = lowerer.emitBinary(Opcode::And, IlType(IlKind::I64), lhsLogical, rhsLogical);
        return {res, IlType(IlKind::I64)};
    }

    if (expr.op == BinaryExpr::Op::LogicalOr)
    {
        Value lhsBool = toBool(lhs);
        Lowerer::RVal rhs = lowerer.lowerExpr(*expr.rhs);
        Value rhsBool = toBool(std::move(rhs));
        lowerer.curLoc = expr.loc;
        Value lhsLogical = lowerer.emitBasicLogicalI64(lhsBool);
        lowerer.curLoc = expr.loc;
        Value rhsLogical = lowerer.emitBasicLogicalI64(rhsBool);
        lowerer.curLoc = expr.loc;
        Value res = lowerer.emitBinary(Opcode::Or, IlType(IlKind::I64), lhsLogical, rhsLogical);
        return {res, IlType(IlKind::I64)};
    }

    if (auto *emitter = lowerer.diagnosticEmitter())
    {
        std::string_view opText = logicalOperatorDisplayName(expr.op);
        std::string message = "unsupported logical operator '";
        message.append(opText);
        message.push_back('\'');
        if (opText == std::string_view("<logical>"))
        {
            message.append(" (enum value ");
            message.append(std::to_string(static_cast<int>(expr.op)));
            message.push_back(')');
        }
        message.append("; assuming FALSE");
        emitter->emit(il::support::Severity::Error,
                      std::string(kDiagUnsupportedLogicalOperator),
                      expr.loc,
                      0,
                      std::move(message));
    }

    lowerer.curLoc = expr.loc;
    Value logicalFalse = lowerer.emitBoolConst(false);
    lowerer.curLoc = expr.loc;
    Value logicalWord = lowerer.emitBasicLogicalI64(logicalFalse);
    return {logicalWord, IlType(IlKind::I64)};
}

Lowerer::RVal Lowerer::lowerLogicalBinary(const BinaryExpr &expr)
{
    return ::il::frontends::basic::lowerLogicalBinary(*this, expr);
}

Lowerer::RVal lowerLogicalBinary(Lowerer &lowerer, const BinaryExpr &expr)
{
    LogicalExprLowering lowering(lowerer);
    return lowering.lower(expr);
}

} // namespace il::frontends::basic
