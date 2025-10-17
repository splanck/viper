//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the lowering logic for BASIC logical expressions.  The helpers
// convert AST-level boolean operators into IL by coordinating with the core
// Lowerer APIs that handle short-circuiting, truthiness coercion, and runtime
// diagnostics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Lowering routines for BASIC logical expressions.
/// @details The translation functions map AST binary logical operators to IL
///          instructions while preserving BASIC's truthiness rules.  Short-
///          circuiting is implemented through dedicated helper callbacks that
///          emit branch-driven control flow when necessary.

#include "frontends/basic/LowerExprLogical.hpp"

#include "frontends/basic/DiagnosticEmitter.hpp"

namespace il::frontends::basic
{
using namespace il::core;

using IlType = il::core::Type;
using IlKind = IlType::Kind;

namespace
{
/// @brief Map a logical operator enumerator to a diagnostic display name.
/// @details Returns the token spelling used in BASIC diagnostics for the
///          supplied operator; a fallback string is provided for unexpected
///          values so error messages remain intelligible.
/// @param op Logical operator enumerator.
/// @return Display name used in diagnostics.
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

/// @brief Construct a logical-expression lowering helper bound to a Lowerer.
/// @details Stores a pointer to the owning @ref Lowerer so subsequent lowering
///          routines can reuse shared facilities such as expression evaluation,
///          boolean coercion, and IL emission.
/// @param lowerer Lowerer instance providing lowering utilities.
LogicalExprLowering::LogicalExprLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

/// @brief Lower a BASIC logical binary expression into IL.
/// @details Evaluates both operands using the underlying @ref Lowerer, coerces
///          them to BASIC boolean semantics, and emits either short-circuiting
///          control flow (for ANDALSO/ORELSE) or direct bitwise operations.  The
///          result is packaged as an @ref Lowerer::RVal retaining the IL value
///          and type information.
/// @param expr AST binary expression describing the logical operation.
/// @return Lowered IL value paired with its IL type.
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

/// @brief Entry point used by @ref Lowerer to lower logical binary expressions.
/// @details Instantiates @ref LogicalExprLowering on demand so the implementation
///          details remain encapsulated in this translation unit.
/// @param expr AST binary expression describing the logical operation.
/// @return Lowered IL value packaged as @ref Lowerer::RVal.
Lowerer::RVal Lowerer::lowerLogicalBinary(const BinaryExpr &expr)
{
    return ::il::frontends::basic::lowerLogicalBinary(*this, expr);
}

/// @brief Helper that forwards to @ref LogicalExprLowering after dependency
///        injection.
/// @details Constructed as a free function so other translation units can lower
///          logical expressions without instantiating the helper class directly.
/// @param lowerer Lowerer responsible for emitting IL.
/// @param expr    AST expression describing the logical operation.
/// @return Lowered IL value packaged as @ref Lowerer::RVal.
Lowerer::RVal lowerLogicalBinary(Lowerer &lowerer, const BinaryExpr &expr)
{
    LogicalExprLowering lowering(lowerer);
    return lowering.lower(expr);
}

} // namespace il::frontends::basic
