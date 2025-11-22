//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the numeric lowering helpers used by the BASIC front end.  The
// routines in this file perform operand coercions, select appropriate IL
// opcodes, and emit runtime helper calls for numerically sensitive operations
// such as exponentiation or string concatenation.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Numeric expression lowering utilities for BASIC.
/// @details Provides the implementation behind `Lowerer` helpers that handle
///          arithmetic, relational comparisons, and mixed-type operations,
///          including specialised handling for string concatenation and
///          constant folding patterns.

#include "frontends/basic/LowerExprNumeric.hpp"
#include "frontends/basic/ASTUtils.hpp"

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

/// @brief Check whether an IL type represents a BASIC integer category.
///
/// @param kind IL type kind to inspect.
/// @return True if the kind is one of the signed integer variants.
bool isIntegerKind(IlKind kind)
{
    return kind == IlKind::I16 || kind == IlKind::I32 || kind == IlKind::I64;
}

/// @brief Choose an integer arithmetic type compatible with both operands.
///
/// @details Prefers the narrowest common type to preserve BASIC's integer
///          semantics when both operands are narrow integers; otherwise promotes
///          to 64-bit.
///
/// @param lhsKind Type of the left-hand operand.
/// @param rhsKind Type of the right-hand operand.
/// @return Result type suitable for integer arithmetic.
IlType integerArithmeticType(IlKind lhsKind, IlKind rhsKind)
{
    if (lhsKind == IlKind::I16 && rhsKind == IlKind::I16)
        return IlType(IlKind::I16);
    if (lhsKind == IlKind::I32 && rhsKind == IlKind::I32)
        return IlType(IlKind::I32);
    return IlType(IlKind::I64);
}

} // namespace

/// @brief Bind the numeric lowering helper to a concrete lowering engine.
///
/// @param lowerer Owning lowering driver that emits IL.
NumericExprLowering::NumericExprLowering(Lowerer &lowerer) noexcept : lowerer_(&lowerer) {}

/// @brief Lower integer division or modulus expressions with overflow checks.
///
/// @details Examines operand types to see whether a narrower integer variant can
///          be used (to match legacy semantics) and emits the appropriate IL
///          opcode with divide-by-zero guards. Integer division (\\) and modulus (MOD)
///          operations use the sdiv.chk0 and srem.chk0 IL instructions, which only
///          support i64 operands. This function coerces both operands to i64 before
///          emitting the operation.
///
/// @param expr Binary expression node representing IDIV or MOD.
/// @return Lowered r-value carrying the operation result.
Lowerer::RVal NumericExprLowering::lowerDivOrMod(const BinaryExpr &expr)
{
    Lowerer &lowerer = *lowerer_;
    Lowerer::RVal lhs = lowerer.lowerExpr(*expr.lhs);
    Lowerer::RVal rhs = lowerer.lowerExpr(*expr.rhs);

    // Note: sdiv.chk0 and srem.chk0 IL instructions only support i64 operands.
    // Always coerce to i64 rather than trying to narrow to i16/i32.
    lhs = lowerer.coerceToI64(std::move(lhs), expr.loc);
    rhs = lowerer.coerceToI64(std::move(rhs), expr.loc);

    Opcode op = (expr.op == BinaryExpr::Op::IDiv) ? Opcode::SDivChk0 : Opcode::SRemChk0;
    IlType resultTy(IlKind::I64);

    lowerer.curLoc = expr.loc;
    Value res = lowerer.emitBinary(op, resultTy, lhs.value, rhs.value);
    return {res, resultTy};
}

/// @brief Normalise operands for a numeric binary operation.
///
/// @details Applies BASIC's promotion and coercion rules to ensure both operands
///          use compatible types, capturing the chosen arithmetic/result type in
///          a configuration structure for later use.
///
/// @param expr Binary expression being lowered.
/// @param lhs Left-hand side value to normalise (updated in place).
/// @param rhs Right-hand side value to normalise (updated in place).
/// @return Configuration describing operand category and result type.
NumericExprLowering::NumericOpConfig NumericExprLowering::normalizeNumericOperands(
    const BinaryExpr &expr, Lowerer::RVal &lhs, Lowerer::RVal &rhs)
{
    Lowerer &lowerer = *lowerer_;
    NumericOpConfig config;

    const bool requiresFloat = expr.op == BinaryExpr::Op::Pow;
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

    const auto *lhsInt = as<const IntExpr>(*expr.lhs);
    const auto *rhsInt = as<const IntExpr>(*expr.rhs);
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

    // Coerce operands to match the chosen arithmetic type (fixes BUG-012: boolean
    // variables are i16, but TRUE/FALSE constants are i64, requiring promotion)
    if (lhs.type.kind != config.arithmeticType.kind)
    {
        if (config.arithmeticType.kind == IlKind::I64)
            lhs = lowerer.coerceToI64(std::move(lhs), expr.loc);
    }
    if (rhs.type.kind != config.arithmeticType.kind)
    {
        if (config.arithmeticType.kind == IlKind::I64)
            rhs = lowerer.coerceToI64(std::move(rhs), expr.loc);
    }

    return config;
}

/// @brief Detect and lower bespoke constant-folding opportunities.
///
/// @details Handles the common `0 - X` negation pattern for integer operands,
///          producing a direct negation rather than emitting subtraction and a
///          zero literal.
///
/// @param expr Binary expression under consideration.
/// @param lhs Normalised left-hand operand.
/// @param rhs Normalised right-hand operand.
/// @param config Operand configuration returned by normalisation.
/// @return Lowered value when a special case applies; `std::nullopt` otherwise.
std::optional<Lowerer::RVal> NumericExprLowering::applySpecialConstantPatterns(
    const BinaryExpr &expr, Lowerer::RVal &lhs, Lowerer::RVal &rhs, const NumericOpConfig &config)
{
    (void)lhs;
    if (expr.op != BinaryExpr::Op::Sub || config.isFloat)
        return std::nullopt;

    const auto *lhsInt = as<const IntExpr>(*expr.lhs);
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

/// @brief Choose the IL opcode used to implement a numeric binary operation.
///
/// @details Takes into account whether the operands are floating-point or
///          integer and whether the operation yields a boolean result, in which
///          case the helper also records whether the boolean must be promoted to
///          BASIC's -1/0 logical representation.
///
/// @param op Binary operator being lowered.
/// @param config Operand configuration describing operand categories.
/// @return Structure containing the opcode, result type, and promotion flags.
NumericExprLowering::OpcodeSelection NumericExprLowering::selectNumericOpcode(
    BinaryExpr::Op op, const NumericOpConfig &config)
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

/// @brief Lower the BASIC exponentiation operator.
///
/// @details Normalises operands to floating point and calls the runtime helper
///          that performs domain-checked exponentiation, recording that the
///          helper must be linked in.
///
/// @param expr AST node for the POW operation.
/// @param lhs Left-hand operand (moved into the helper).
/// @param rhs Right-hand operand (moved into the helper).
/// @return Lowered value representing the power result.
Lowerer::RVal NumericExprLowering::lowerPowBinary(const BinaryExpr &expr,
                                                  Lowerer::RVal lhs,
                                                  Lowerer::RVal rhs)
{
    Lowerer &lowerer = *lowerer_;
    NumericOpConfig config = normalizeNumericOperands(expr, lhs, rhs);
    lowerer.trackRuntime(Lowerer::RuntimeFeature::Pow);
    lowerer.curLoc = expr.loc;
    Value res =
        lowerer.emitCallRet(IlType(IlKind::F64), "rt_pow_f64_chkdom", {lhs.value, rhs.value});
    IlType resultType =
        (config.resultType.kind == IlKind::Void) ? IlType(IlKind::F64) : config.resultType;
    return {res, resultType};
}

/// @brief Lower binary operations when operands are strings.
///
/// @details Supports concatenation via `Viper.Strings.Concat` and equality/inequality tests
///          via dedicated runtime helpers that preserve BASIC's string
///          comparison semantics.
///
/// @param expr AST node for the string operation.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered value storing the operation result.
Lowerer::RVal NumericExprLowering::lowerStringBinary(const BinaryExpr &expr,
                                                     Lowerer::RVal lhs,
                                                     Lowerer::RVal rhs)
{
    Lowerer &lowerer = *lowerer_;
    lowerer.curLoc = expr.loc;
    if (expr.op == BinaryExpr::Op::Add)
    {
        // Ensure runtime signature is linked for string concatenation.
        lowerer.trackRuntime(Lowerer::RuntimeFeature::Concat);
        // BUG-110: Avoid per-iteration alloca spills; pass operands directly.
        Value res = lowerer.emitCallRet(IlType(IlKind::Str), "Viper.Strings.Concat", {lhs.value, rhs.value});
        lowerer.deferReleaseStr(res);
        return {res, IlType(IlKind::Str)};
    }

    // String comparison operators - select appropriate runtime function
    const char *rtFunc = nullptr;
    bool needsNegation = false;

    switch (expr.op)
    {
        case BinaryExpr::Op::Eq:
            rtFunc = "rt_str_eq";
            break;
        case BinaryExpr::Op::Ne:
            rtFunc = "rt_str_eq";
            needsNegation = true;
            break;
        case BinaryExpr::Op::Lt:
            rtFunc = "rt_str_lt";
            break;
        case BinaryExpr::Op::Le:
            rtFunc = "rt_str_le";
            break;
        case BinaryExpr::Op::Gt:
            rtFunc = "rt_str_gt";
            break;
        case BinaryExpr::Op::Ge:
            rtFunc = "rt_str_ge";
            break;
        default:
            // Should not reach here - validated by semantic analysis
            rtFunc = "rt_str_eq";
            break;
    }

    Value cmp = lowerer.emitCallRet(lowerer.ilBoolTy(), rtFunc, {lhs.value, rhs.value});
    Value cmpLogical = lowerer.emitBasicLogicalI64(cmp);

    if (needsNegation)
    {
        Value res = lowerer.emitCommon(expr.loc).logical_xor(cmpLogical, lowerer.emitConstI64(-1));
        return {res, IlType(IlKind::I64)};
    }

    return {cmpLogical, IlType(IlKind::I64)};
}

/// @brief Lower arithmetic or comparison operators on numeric operands.
///
/// @details Normalises operands, applies special constant folding, selects the
///          appropriate opcode, and emits the final IL.  Comparison results are
///          expanded to BASIC's logical -1/0 representation when required.
///
/// @param expr AST node for the binary operation.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered r-value carrying the operation result.
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

/// @brief Entry point on `Lowerer` for lowering division or modulus.
///
/// @param expr Binary expression node representing IDIV or MOD.
/// @return Lowered value after delegating to `NumericExprLowering`.
Lowerer::RVal Lowerer::lowerDivOrMod(const BinaryExpr &expr)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerDivOrMod(expr);
}

/// @brief Entry point for lowering exponentiation with explicit operands.
///
/// @param expr Binary expression node.
/// @param lhs Left-hand operand already partially lowered.
/// @param rhs Right-hand operand already partially lowered.
/// @return Lowered value computed by the numeric helper.
Lowerer::RVal Lowerer::lowerPowBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerPowBinary(expr, std::move(lhs), std::move(rhs));
}

/// @brief Entry point for lowering string-aware binary operations.
///
/// @param expr Binary expression node describing the operation.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered value generated by the string helper.
Lowerer::RVal Lowerer::lowerStringBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerStringBinary(expr, std::move(lhs), std::move(rhs));
}

/// @brief Entry point for lowering generic numeric binary operations.
///
/// @param expr Binary expression node.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered value after delegating to `NumericExprLowering`.
Lowerer::RVal Lowerer::lowerNumericBinary(const BinaryExpr &expr, RVal lhs, RVal rhs)
{
    NumericExprLowering lowering(*this);
    return lowering.lowerNumericBinary(expr, std::move(lhs), std::move(rhs));
}

/// @brief Free-function wrapper for lowering division or modulus.
///
/// @param lowerer Lowering engine to use.
/// @param expr Binary expression to lower.
/// @return Lowered value provided by the helper.
Lowerer::RVal lowerDivOrMod(Lowerer &lowerer, const BinaryExpr &expr)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerDivOrMod(expr);
}

/// @brief Free-function wrapper that lowers exponentiation expressions.
///
/// @param lowerer Lowering engine to use.
/// @param expr Binary exponentiation node.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered value from the numeric helper.
Lowerer::RVal lowerPowBinary(Lowerer &lowerer,
                             const BinaryExpr &expr,
                             Lowerer::RVal lhs,
                             Lowerer::RVal rhs)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerPowBinary(expr, std::move(lhs), std::move(rhs));
}

/// @brief Free-function wrapper that lowers string binary operations.
///
/// @param lowerer Lowering engine to use.
/// @param expr Binary expression node.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered value emitted by the helper.
Lowerer::RVal lowerStringBinary(Lowerer &lowerer,
                                const BinaryExpr &expr,
                                Lowerer::RVal lhs,
                                Lowerer::RVal rhs)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerStringBinary(expr, std::move(lhs), std::move(rhs));
}

/// @brief Free-function wrapper that lowers generic numeric binary operations.
///
/// @param lowerer Lowering engine to use.
/// @param expr Binary expression node.
/// @param lhs Left-hand operand.
/// @param rhs Right-hand operand.
/// @return Lowered value provided by the helper.
Lowerer::RVal lowerNumericBinary(Lowerer &lowerer,
                                 const BinaryExpr &expr,
                                 Lowerer::RVal lhs,
                                 Lowerer::RVal rhs)
{
    NumericExprLowering lowering(lowerer);
    return lowering.lowerNumericBinary(expr, std::move(lhs), std::move(rhs));
}

} // namespace il::frontends::basic
