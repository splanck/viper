// File: src/frontends/basic/LowerExprNumeric.cpp
// License: MIT License. See LICENSE in the project root for full license information.
// Purpose: Implements numeric expression lowering helpers for the BASIC Lowerer.
// Key invariants: Numeric helpers share Lowerer coercions and runtime tracking
//                 utilities to preserve consistent semantics.
// Ownership/Lifetime: Helpers borrow the Lowerer only for the duration of a
//                      single lowering call.
// Links: docs/codemap.md

#include "frontends/basic/LowerExprNumeric.hpp"

#include "frontends/basic/TypeSuffix.hpp"

#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace il::frontends::basic
{
using namespace il::core;

using IlType = il::core::Type;
using IlKind = IlType::Kind;

namespace
{

bool isIntegerKind(IlKind kind)
{
    return kind == IlKind::I16 || kind == IlKind::I32 || kind == IlKind::I64;
}

IlType integerArithmeticType(IlKind lhsKind, IlKind rhsKind)
{
    if (lhsKind == IlKind::I16 && rhsKind == IlKind::I16)
        return IlType(IlKind::I16);
    if (lhsKind == IlKind::I32 && rhsKind == IlKind::I32)
        return IlType(IlKind::I32);
    return IlType(IlKind::I64);
}

} // namespace

NumericExprLowering::NumericExprLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

Lowerer::RVal NumericExprLowering::lowerDivOrMod(const BinaryExpr &expr)
{
    Lowerer &lowerer = *lowerer_;
    Lowerer::RVal lhs = lowerer.lowerExpr(*expr.lhs);
    Lowerer::RVal rhs = lowerer.lowerExpr(*expr.rhs);

    std::function<std::optional<IlKind>(const Expr &, const Lowerer::RVal &)> classifyIntegerRank;
    classifyIntegerRank = [&](const Expr &node, const Lowerer::RVal &val) -> std::optional<IlKind>
    {
        using Kind = IlKind;
        switch (val.type.kind)
        {
            case Kind::I16:
                return Kind::I16;
            case Kind::I32:
                return Kind::I32;
            case Kind::F64:
            case Kind::Str:
            case Kind::Ptr:
            case Kind::I1:
            case Kind::Error:
            case Kind::ResumeTok:
            case Kind::Void:
                return std::nullopt;
            case Kind::I64:
                break;
        }

        if (const auto *intLit = dynamic_cast<const IntExpr *>(&node))
        {
            if (intLit->value >= std::numeric_limits<int16_t>::min() &&
                intLit->value <= std::numeric_limits<int16_t>::max())
                return Kind::I16;
            if (intLit->value >= std::numeric_limits<int32_t>::min() &&
                intLit->value <= std::numeric_limits<int32_t>::max())
                return Kind::I32;
            return std::nullopt;
        }
        if (const auto *var = dynamic_cast<const VarExpr *>(&node))
        {
            if (const auto *info = lowerer.findSymbol(var->name))
            {
                if (info->hasType)
                {
                    if (info->type == Lowerer::AstType::F64)
                        return std::nullopt;
                }
            }
            Lowerer::AstType astTy = inferAstTypeFromName(var->name);
            if (astTy == Lowerer::AstType::F64)
                return std::nullopt;
            return Kind::I16;
        }
        if (const auto *unary = dynamic_cast<const UnaryExpr *>(&node))
        {
            if (unary->expr)
                return classifyIntegerRank(*unary->expr, val);
        }
        return std::nullopt;
    };

    auto narrowTo = [&](const Expr &node, Lowerer::RVal value, IlType target)
    {
        if (value.type.kind == target.kind)
            return value;
        value = lowerer.coerceToI64(std::move(value), node.loc);
        lowerer.curLoc = node.loc;
        value.value = lowerer.emitUnary(Opcode::CastSiNarrowChk, target, value.value);
        value.type = target;
        return value;
    };

    std::optional<IlKind> lhsRank = classifyIntegerRank(*expr.lhs, lhs);
    std::optional<IlKind> rhsRank = classifyIntegerRank(*expr.rhs, rhs);

    Opcode op = (expr.op == BinaryExpr::Op::IDiv) ? Opcode::SDivChk0 : Opcode::SRemChk0;
    IlType resultTy(IlKind::I64);

    if (lhsRank && rhsRank)
    {
        IlKind promoted =
            (*lhsRank == IlKind::I32 || *rhsRank == IlKind::I32) ? IlKind::I32 : IlKind::I16;
        resultTy = IlType(promoted);
        lhs = narrowTo(*expr.lhs, std::move(lhs), resultTy);
        rhs = narrowTo(*expr.rhs, std::move(rhs), resultTy);
    }
    else
    {
        lhs = lowerer.coerceToI64(std::move(lhs), expr.loc);
        rhs = lowerer.coerceToI64(std::move(rhs), expr.loc);
        resultTy = IlType(IlKind::I64);
    }

    lowerer.curLoc = expr.loc;
    Value res = lowerer.emitBinary(op, resultTy, lhs.value, rhs.value);
    return {res, resultTy};
}

NumericExprLowering::NumericOpConfig NumericExprLowering::normalizeNumericOperands(
    const BinaryExpr &expr,
    Lowerer::RVal &lhs,
    Lowerer::RVal &rhs)
{
    Lowerer &lowerer = *lowerer_;
    NumericOpConfig config;

    const bool requiresFloat =
        expr.op == BinaryExpr::Op::Div || expr.op == BinaryExpr::Op::Pow;
    if (requiresFloat)
    {
        auto promoteToF64 = [&](Lowerer::RVal &value, const Expr *node)
        {
            if (value.type.kind == IlKind::F64)
                return;
            il::support::SourceLoc loc = node ? node->loc : expr.loc;
            if (expr.op == BinaryExpr::Op::Div)
            {
                value = lowerer.coerceToI64(std::move(value), loc);
                lowerer.curLoc = loc;
                value.value =
                    lowerer.emitUnary(Opcode::CastSiToFp, IlType(IlKind::F64), value.value);
                value.type = IlType(IlKind::F64);
            }
            else
            {
                value = lowerer.ensureF64(std::move(value), loc);
            }
        };

        promoteToF64(lhs, expr.lhs.get());
        promoteToF64(rhs, expr.rhs.get());
        config.isFloat = true;
        config.arithmeticType = IlType(IlKind::F64);
        config.resultType = IlType(IlKind::F64);
        return config;
    }

    if (lhs.type.kind == IlKind::F64 || rhs.type.kind == IlKind::F64)
    {
        lhs = lowerer.coerceToF64(std::move(lhs), expr.loc);
        rhs = lowerer.coerceToF64(std::move(rhs), expr.loc);
        config.isFloat = true;
        config.arithmeticType = IlType(IlKind::F64);
        config.resultType = IlType(IlKind::F64);
        return config;
    }

    config.isFloat = false;
    config.arithmeticType = integerArithmeticType(lhs.type.kind, rhs.type.kind);
    config.resultType = config.arithmeticType;

    const auto *lhsInt = dynamic_cast<const IntExpr *>(expr.lhs.get());
    const auto *rhsInt = dynamic_cast<const IntExpr *>(expr.rhs.get());
    if (lhsInt && rhsInt)
    {
        const auto fits16 = [](long long v)
        {
            return v >= std::numeric_limits<int16_t>::min() &&
                   v <= std::numeric_limits<int16_t>::max();
        };
        const auto fits32 = [](long long v)
        {
            return v >= std::numeric_limits<int32_t>::min() &&
                   v <= std::numeric_limits<int32_t>::max();
        };
        if (fits16(lhsInt->value) && fits16(rhsInt->value))
        {
            config.arithmeticType = IlType(IlKind::I16);
            config.resultType = config.arithmeticType;
        }
        else if (fits32(lhsInt->value) && fits32(rhsInt->value))
        {
            config.arithmeticType = IlType(IlKind::I32);
            config.resultType = config.arithmeticType;
        }
    }

    return config;
}

std::optional<Lowerer::RVal> NumericExprLowering::applySpecialConstantPatterns(
    const BinaryExpr &expr,
    Lowerer::RVal &lhs,
    Lowerer::RVal &rhs,
    const NumericOpConfig &config)
{
    (void)lhs;
    if (expr.op != BinaryExpr::Op::Sub || config.isFloat)
        return std::nullopt;

    const auto *lhsInt = dynamic_cast<const IntExpr *>(expr.lhs.get());
    if (!lhsInt || lhsInt->value != 0)
        return std::nullopt;

    if (!isIntegerKind(rhs.type.kind))
        return std::nullopt;

    if (rhs.type.kind != IlKind::I16 && rhs.type.kind != IlKind::I32)
        return std::nullopt;

    Lowerer &lowerer = *lowerer_;
    lowerer.curLoc = expr.loc;
    Value neg = lowerer.emitCheckedNeg(rhs.type, rhs.value);
    return Lowerer::RVal{neg, rhs.type};
}

NumericExprLowering::OpcodeSelection NumericExprLowering::selectNumericOpcode(
    BinaryExpr::Op op,
    const NumericOpConfig &config)
{
    Lowerer &lowerer = *lowerer_;
    OpcodeSelection selection;
    selection.resultType = config.arithmeticType;

    switch (op)
    {
        case BinaryExpr::Op::Add:
            selection.opcode = config.isFloat ? Opcode::FAdd : Opcode::IAddOvf;
            break;
        case BinaryExpr::Op::Sub:
            selection.opcode = config.isFloat ? Opcode::FSub : Opcode::ISubOvf;
            break;
        case BinaryExpr::Op::Mul:
            selection.opcode = config.isFloat ? Opcode::FMul : Opcode::IMulOvf;
            break;
        case BinaryExpr::Op::Div:
            if (config.isFloat)
            {
                selection.opcode = Opcode::FDiv;
                selection.resultType = config.arithmeticType;
            }
            else
            {
                selection.opcode = Opcode::SDivChk0;
                selection.resultType = IlType(IlKind::I64);
            }
            break;
        case BinaryExpr::Op::Eq:
            selection.opcode = config.isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            selection.resultType = lowerer.ilBoolTy();
            selection.promoteBoolToI64 = true;
            break;
        case BinaryExpr::Op::Ne:
            selection.opcode = config.isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            selection.resultType = lowerer.ilBoolTy();
            selection.promoteBoolToI64 = true;
            break;
        case BinaryExpr::Op::Lt:
            selection.opcode = config.isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            selection.resultType = lowerer.ilBoolTy();
            selection.promoteBoolToI64 = true;
            break;
        case BinaryExpr::Op::Le:
            selection.opcode = config.isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            selection.resultType = lowerer.ilBoolTy();
            selection.promoteBoolToI64 = true;
            break;
        case BinaryExpr::Op::Gt:
            selection.opcode = config.isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            selection.resultType = lowerer.ilBoolTy();
            selection.promoteBoolToI64 = true;
            break;
        case BinaryExpr::Op::Ge:
            selection.opcode = config.isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            selection.resultType = lowerer.ilBoolTy();
            selection.promoteBoolToI64 = true;
            break;
        default:
            break;
    }

    return selection;
}

Lowerer::RVal NumericExprLowering::lowerPowBinary(const BinaryExpr &expr,
                                                  Lowerer::RVal lhs,
                                                  Lowerer::RVal rhs)
{
    Lowerer &lowerer = *lowerer_;
    NumericOpConfig config = normalizeNumericOperands(expr, lhs, rhs);
    lowerer.trackRuntime(Lowerer::RuntimeFeature::Pow);
    lowerer.curLoc = expr.loc;
    Value res = lowerer.emitCallRet(
        IlType(IlKind::F64), "rt_pow_f64_chkdom", {lhs.value, rhs.value});
    IlType resultType =
        (config.resultType.kind == IlKind::Void) ? IlType(IlKind::F64) : config.resultType;
    return {res, resultType};
}

Lowerer::RVal NumericExprLowering::lowerStringBinary(const BinaryExpr &expr,
                                                     Lowerer::RVal lhs,
                                                     Lowerer::RVal rhs)
{
    Lowerer &lowerer = *lowerer_;
    lowerer.curLoc = expr.loc;
    if (expr.op == BinaryExpr::Op::Add)
    {
        Value res = lowerer.emitCallRet(IlType(IlKind::Str), "rt_concat", {lhs.value, rhs.value});
        return {res, IlType(IlKind::Str)};
    }
    Value eq = lowerer.emitCallRet(lowerer.ilBoolTy(), "rt_str_eq", {lhs.value, rhs.value});
    Value eqLogical = lowerer.emitBasicLogicalI64(eq);
    if (expr.op == BinaryExpr::Op::Ne)
    {
        lowerer.curLoc = expr.loc;
        Value res = lowerer.emitBinary(
            Opcode::Xor, IlType(IlKind::I64), eqLogical, lowerer.emitConstI64(-1));
        return {res, IlType(IlKind::I64)};
    }
    return {eqLogical, IlType(IlKind::I64)};
}

Lowerer::RVal NumericExprLowering::lowerNumericBinary(const BinaryExpr &expr,
                                                      Lowerer::RVal lhs,
                                                      Lowerer::RVal rhs)
{
    Lowerer &lowerer = *lowerer_;
    NumericOpConfig config = normalizeNumericOperands(expr, lhs, rhs);

    if (auto special = applySpecialConstantPatterns(expr, lhs, rhs, config))
        return *special;

    OpcodeSelection selection = selectNumericOpcode(expr.op, config);
    lowerer.curLoc = expr.loc;
    Value res = lowerer.emitBinary(selection.opcode, selection.resultType, lhs.value, rhs.value);
    if (selection.promoteBoolToI64)
    {
        lowerer.curLoc = expr.loc;
        Value logical = lowerer.emitBasicLogicalI64(res);
        return {logical, IlType(IlKind::I64)};
    }
    return {res, selection.resultType};
}

Lowerer::RVal Lowerer::lowerDivOrMod(const BinaryExpr &expr)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerDivOrMod(expr);
}

Lowerer::RVal Lowerer::lowerPowBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerPowBinary(expr, std::move(lhs), std::move(rhs));
}

Lowerer::RVal Lowerer::lowerStringBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerStringBinary(expr, std::move(lhs), std::move(rhs));
}

Lowerer::RVal Lowerer::lowerNumericBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerNumericBinary(expr, std::move(lhs), std::move(rhs));
}

Lowerer::RVal lowerDivOrMod(Lowerer &lowerer, const BinaryExpr &expr)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerDivOrMod(expr);
}

Lowerer::RVal lowerPowBinary(Lowerer &lowerer,
                             const BinaryExpr &expr,
                             Lowerer::RVal lhs,
                             Lowerer::RVal rhs)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerPowBinary(expr, std::move(lhs), std::move(rhs));
}

Lowerer::RVal lowerStringBinary(Lowerer &lowerer,
                                const BinaryExpr &expr,
                                Lowerer::RVal lhs,
                                Lowerer::RVal rhs)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerStringBinary(expr, std::move(lhs), std::move(rhs));
}

Lowerer::RVal lowerNumericBinary(Lowerer &lowerer,
                                 const BinaryExpr &expr,
                                 Lowerer::RVal lhs,
                                 Lowerer::RVal rhs)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerNumericBinary(expr, std::move(lhs), std::move(rhs));
}

} // namespace il::frontends::basic
