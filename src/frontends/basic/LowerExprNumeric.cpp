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

Lowerer::RVal NumericExprLowering::lowerPowBinary(const BinaryExpr &expr,
                                                  Lowerer::RVal lhs,
                                                  Lowerer::RVal rhs)
{
    il::support::SourceLoc lhsLoc = expr.lhs ? expr.lhs->loc : expr.loc;
    il::support::SourceLoc rhsLoc = expr.rhs ? expr.rhs->loc : expr.loc;
    Lowerer &lowerer = *lowerer_;
    lhs = lowerer.ensureF64(std::move(lhs), lhsLoc);
    rhs = lowerer.ensureF64(std::move(rhs), rhsLoc);
    lowerer.trackRuntime(Lowerer::RuntimeFeature::Pow);
    lowerer.curLoc = expr.loc;
    Value res =
        lowerer.emitCallRet(IlType(IlKind::F64), "rt_pow_f64_chkdom", {lhs.value, rhs.value});
    return {res, IlType(IlKind::F64)};
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
    lowerer.curLoc = expr.loc;
    if (expr.op == BinaryExpr::Op::Div)
    {
        auto promoteToF64 = [&](Lowerer::RVal value, const Expr &node)
        {
            if (value.type.kind == IlKind::F64)
                return value;
            value = lowerer.coerceToI64(std::move(value), node.loc);
            lowerer.curLoc = node.loc;
            value.value = lowerer.emitUnary(Opcode::CastSiToFp, IlType(IlKind::F64), value.value);
            value.type = IlType(IlKind::F64);
            return value;
        };

        if (expr.lhs)
            lhs = promoteToF64(std::move(lhs), *expr.lhs);
        else
            lhs = promoteToF64(std::move(lhs), expr);
        if (expr.rhs)
            rhs = promoteToF64(std::move(rhs), *expr.rhs);
        else
            rhs = promoteToF64(std::move(rhs), expr);

        lowerer.curLoc = expr.loc;
        Value res = lowerer.emitBinary(Opcode::FDiv, IlType(IlKind::F64), lhs.value, rhs.value);
        return {res, IlType(IlKind::F64)};
    }

    if (lhs.type.kind == IlKind::I64 && rhs.type.kind == IlKind::F64)
    {
        lhs = lowerer.coerceToF64(std::move(lhs), expr.loc);
    }
    else if (lhs.type.kind == IlKind::F64 && rhs.type.kind == IlKind::I64)
    {
        rhs = lowerer.coerceToF64(std::move(rhs), expr.loc);
    }
    auto isIntegerKind = [](IlKind kind)
    { return kind == IlKind::I16 || kind == IlKind::I32 || kind == IlKind::I64; };
    bool isFloat = lhs.type.kind == IlKind::F64;
    if (!isFloat && expr.op == BinaryExpr::Op::Sub)
    {
        if (const auto *lhsInt = dynamic_cast<const IntExpr *>(expr.lhs.get());
            lhsInt && lhsInt->value == 0 && isIntegerKind(rhs.type.kind) &&
            (rhs.type.kind == IlKind::I16 || rhs.type.kind == IlKind::I32))
        {
            Value neg = lowerer.emitCheckedNeg(rhs.type, rhs.value);
            return {neg, rhs.type};
        }
    }
    auto integerArithmeticType = [](IlKind lhsKind, IlKind rhsKind)
    {
        if (lhsKind == IlKind::I16 && rhsKind == IlKind::I16)
            return IlType(IlKind::I16);
        if (lhsKind == IlKind::I32 && rhsKind == IlKind::I32)
            return IlType(IlKind::I32);
        return IlType(IlKind::I64);
    };
    Opcode op = Opcode::IAddOvf;
    IlType arithTy =
        isFloat ? IlType(IlKind::F64) : integerArithmeticType(lhs.type.kind, rhs.type.kind);
    if (!isFloat)
    {
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
                arithTy = IlType(IlKind::I16);
            }
            else if (fits32(lhsInt->value) && fits32(rhsInt->value))
            {
                arithTy = IlType(IlKind::I32);
            }
        }
    }
    IlType ty = arithTy;
    switch (expr.op)
    {
        case BinaryExpr::Op::Add:
            op = isFloat ? Opcode::FAdd : Opcode::IAddOvf;
            break;
        case BinaryExpr::Op::Sub:
            op = isFloat ? Opcode::FSub : Opcode::ISubOvf;
            break;
        case BinaryExpr::Op::Mul:
            op = isFloat ? Opcode::FMul : Opcode::IMulOvf;
            break;
        case BinaryExpr::Op::Div:
            op = isFloat ? Opcode::FDiv : Opcode::SDivChk0;
            ty = isFloat ? arithTy : IlType(IlKind::I64);
            break;
        case BinaryExpr::Op::Eq:
            op = isFloat ? Opcode::FCmpEQ : Opcode::ICmpEq;
            ty = lowerer.ilBoolTy();
            break;
        case BinaryExpr::Op::Ne:
            op = isFloat ? Opcode::FCmpNE : Opcode::ICmpNe;
            ty = lowerer.ilBoolTy();
            break;
        case BinaryExpr::Op::Lt:
            op = isFloat ? Opcode::FCmpLT : Opcode::SCmpLT;
            ty = lowerer.ilBoolTy();
            break;
        case BinaryExpr::Op::Le:
            op = isFloat ? Opcode::FCmpLE : Opcode::SCmpLE;
            ty = lowerer.ilBoolTy();
            break;
        case BinaryExpr::Op::Gt:
            op = isFloat ? Opcode::FCmpGT : Opcode::SCmpGT;
            ty = lowerer.ilBoolTy();
            break;
        case BinaryExpr::Op::Ge:
            op = isFloat ? Opcode::FCmpGE : Opcode::SCmpGE;
            ty = lowerer.ilBoolTy();
            break;
        default:
            break;
    }
    Value res = lowerer.emitBinary(op, ty, lhs.value, rhs.value);
    if (ty.kind == IlKind::I1)
    {
        lowerer.curLoc = expr.loc;
        Value logical = lowerer.emitBasicLogicalI64(res);
        return {logical, IlType(IlKind::I64)};
    }
    return {res, ty};
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
