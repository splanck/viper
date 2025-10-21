//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/frontends/basic/LowerExprLogical.cpp
// Purpose: Implements logical expression lowering helpers for the BASIC Lowerer.
// Key invariants: Logical operators preserve BASIC truthiness semantics using
//                 Lowerer utilities for short-circuiting.
// Ownership/Lifetime: Helpers borrow the Lowerer for the duration of the call.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/LowerExprLogical.hpp"

#include "frontends/basic/DiagnosticEmitter.hpp"

namespace il::frontends::basic
{
using namespace il::core;

using IlType = il::core::Type;
using IlKind = IlType::Kind;

namespace
{
/// @brief Provide a user-facing display name for logical operators.
///
/// @details Converts the lowering-time enumeration into the BASIC keyword used
///          in diagnostics.  Unknown operators fall back to a sentinel so that
///          callers can include the numeric value when forming error messages.
///
/// @param op Logical operator enumeration from the AST.
/// @return BASIC keyword string or `"<logical>"` for unrecognised values.
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

/// @brief Construct the logical lowering helper around a @ref Lowerer.
///
/// @param lowerer Active lowering context used to emit IL instructions.
LogicalExprLowering::LogicalExprLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

/// @brief Lower a BASIC logical binary expression into IL.
///
/// @details Handles both short-circuit (`ANDALSO`, `ORELSE`) and eager
///          (`AND`, `OR`) operators.  Short-circuit operators delegate to
///          @ref Lowerer::lowerBoolBranchExpr to build explicit control flow
///          while eager ones coerce operands to logical words and emit bitwise
///          operations.  Unsupported operators emit diagnostics and return
///          `FALSE` to keep compilation progressing.
///
/// @param expr Logical binary expression AST node.
/// @return Resulting IL value paired with its logical word type.
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

/// @brief Member façade that forwards logical lowering to the helper module.
///
/// @param expr Logical binary expression to lower.
/// @return Result of @ref lowerLogicalBinary.
Lowerer::RVal Lowerer::lowerLogicalBinary(const BinaryExpr &expr)
{
    return ::il::frontends::basic::lowerLogicalBinary(*this, expr);
}

/// @brief Free-function entry point used by tests and other lowering helpers.
///
/// @param lowerer Active lowering context receiving the emitted IL.
/// @param expr Logical binary expression under translation.
/// @return Lowered IL value representing the BASIC logical result.
Lowerer::RVal lowerLogicalBinary(Lowerer &lowerer, const BinaryExpr &expr)
{
    LogicalExprLowering lowering(lowerer);
    return lowering.lower(expr);
}

} // namespace il::frontends::basic
