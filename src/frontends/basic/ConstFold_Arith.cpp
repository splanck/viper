//===----------------------------------------------------------------------===//
//
// This file is part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements arithmetic constant folding helpers for the BASIC front end.  The
// routines apply the language's promotion and overflow rules so literal
// expression trees can be simplified ahead of lowering without changing runtime
// semantics.  Callers receive freshly allocated AST nodes that mirror the
// folded result, enabling aggressive fold attempts without mutating the input
// tree.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Arithmetic constant folding implementation for BASIC expressions.
/// @details Provides helpers that collapse unary and binary arithmetic,
///          comparisons, and boolean operations when both operands are
///          statically known.  The utilities cooperate with the shared
///          promotion logic defined in @ref ConstFoldHelpers.hpp.

#include "frontends/basic/ConstFold_Arith.hpp"

#include "frontends/basic/ConstFoldHelpers.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace il::frontends::basic::detail
{
namespace
{
/// @brief Perform two's-complement addition with wrap-around semantics.
///
/// BASIC integer arithmetic uses 64-bit wrapping behaviour.  Casting the
/// operands to unsigned values mirrors the machine-level wrap before converting
/// back to a signed representation for further processing.
///
/// @param a First operand.
/// @param b Second operand.
/// @return Sum modulo 2^64 converted back to signed form.
long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

/// @brief Perform two's-complement subtraction with wrap-around semantics.
///
/// Mirrors @ref wrapAdd but computes the difference `a - b` modulo 2^64 to
/// reflect BASIC overflow behaviour.
///
/// @param a Minuend operand.
/// @param b Subtrahend operand.
/// @return Difference modulo 2^64 as a signed integer.
long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

/// @brief Perform two's-complement multiplication with wrap-around semantics.
///
/// Multiplication is performed in unsigned space so overflow simply wraps.  The
/// result is then converted back to the signed domain to match BASIC runtime
/// behaviour.
///
/// @param a First operand.
/// @param b Second operand.
/// @return Product modulo 2^64.
long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

/// @brief Fold addition of two numeric literals.
///
/// Promotes operands according to BASIC rules before performing integer or
/// floating-point addition.  When both operands are 16-bit integers and the
/// result overflows the range required by the BASIC spec, folding is aborted to
/// let runtime semantics report the overflow.
///
/// @param lhsRaw Left-hand operand prior to promotion.
/// @param rhsRaw Right-hand operand prior to promotion.
/// @return Folded numeric value or std::nullopt if folding is unsafe.
std::optional<Numeric> foldAdd(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (!lhs.isFloat && !rhs.isFloat)
    {
        const auto minI16 = std::numeric_limits<int16_t>::min();
        const auto maxI16 = std::numeric_limits<int16_t>::max();
        const bool lhsFitsI16 = lhs.i >= minI16 && lhs.i <= maxI16;
        const bool rhsFitsI16 = rhs.i >= minI16 && rhs.i <= maxI16;
        long long sum = wrapAdd(lhs.i, rhs.i);
        if (lhsFitsI16 && rhsFitsI16 && (sum < minI16 || sum > maxI16))
            return std::nullopt;
        return Numeric{false, static_cast<double>(sum), sum};
    }

    double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
    double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
    double result = lv + rv;
    return Numeric{true, result, static_cast<long long>(result)};
}

/// @brief Fold subtraction of two numeric literals.
///
/// Applies the same promotion rules as @ref foldAdd and either performs wrapped
/// integer subtraction or floating-point subtraction depending on the promoted
/// operand types.
///
/// @param lhsRaw Left-hand operand before promotion.
/// @param rhsRaw Right-hand operand before promotion.
/// @return Folded result or std::nullopt when folding is not permitted.
std::optional<Numeric> foldSub(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (!lhs.isFloat && !rhs.isFloat)
    {
        long long diff = wrapSub(lhs.i, rhs.i);
        return Numeric{false, static_cast<double>(diff), diff};
    }

    double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
    double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
    double result = lv - rv;
    return Numeric{true, result, static_cast<long long>(result)};
}

/// @brief Fold multiplication of two numeric literals.
///
/// Performs wrapped integer multiplication or floating-point multiplication
/// according to the promoted operand types.
///
/// @param lhsRaw Left-hand operand prior to promotion.
/// @param rhsRaw Right-hand operand prior to promotion.
/// @return Folded product or std::nullopt when folding cannot be performed.
std::optional<Numeric> foldMul(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (!lhs.isFloat && !rhs.isFloat)
    {
        long long prod = wrapMul(lhs.i, rhs.i);
        return Numeric{false, static_cast<double>(prod), prod};
    }

    double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
    double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
    double result = lv * rv;
    return Numeric{true, result, static_cast<long long>(result)};
}

/// @brief Fold floating-point division of two numeric literals.
///
/// Division always yields a floating-point result in BASIC.  When the divisor is
/// zero folding is aborted so the runtime can raise the appropriate exception.
///
/// @param lhsRaw Numerator prior to promotion.
/// @param rhsRaw Denominator prior to promotion.
/// @return Folded quotient or std::nullopt when division by zero occurs.
std::optional<Numeric> foldDiv(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
    if (rv == 0.0)
        return std::nullopt;

    double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
    double v = lv / rv;
    return Numeric{true, v, static_cast<long long>(v)};
}

/// @brief Fold integer division of two numeric literals.
///
/// Only executes when both operands are integer-valued and the divisor is
/// non-zero.  Otherwise the operation cannot be folded safely.
///
/// @param lhsRaw Dividend prior to promotion.
/// @param rhsRaw Divisor prior to promotion.
/// @return Folded quotient or std::nullopt when folding is not possible.
std::optional<Numeric> foldIDiv(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (lhs.isFloat || rhs.isFloat || rhs.i == 0)
        return std::nullopt;

    long long v = lhs.i / rhs.i;
    return Numeric{false, static_cast<double>(v), v};
}

/// @brief Fold modulo of two integer literals.
///
/// Requires both operands to be integers and the divisor to be non-zero.  The
/// behaviour matches BASIC's integer remainder semantics.
///
/// @param lhsRaw Dividend prior to promotion.
/// @param rhsRaw Divisor prior to promotion.
/// @return Folded remainder or std::nullopt when operands are invalid.
std::optional<Numeric> foldMod(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (lhs.isFloat || rhs.isFloat || rhs.i == 0)
        return std::nullopt;

    long long v = lhs.i % rhs.i;
    return Numeric{false, static_cast<double>(v), v};
}

/// @brief Evaluate a floating-point comparison.
///
/// @param op Comparison operator being applied.
/// @param lhs Left-hand value.
/// @param rhs Right-hand value.
/// @return True when the comparison holds; false otherwise.
bool compareFloat(BinaryExpr::Op op, double lhs, double rhs)
{
    switch (op)
    {
        case BinaryExpr::Op::Eq:
            return lhs == rhs;
        case BinaryExpr::Op::Ne:
            return lhs != rhs;
        case BinaryExpr::Op::Lt:
            return lhs < rhs;
        case BinaryExpr::Op::Le:
            return lhs <= rhs;
        case BinaryExpr::Op::Gt:
            return lhs > rhs;
        case BinaryExpr::Op::Ge:
            return lhs >= rhs;
        default:
            return false;
    }
}

/// @brief Evaluate an integer comparison.
///
/// @param op Comparison operator being applied.
/// @param lhs Left-hand value.
/// @param rhs Right-hand value.
/// @return True when the comparison holds; false otherwise.
bool compareInt(BinaryExpr::Op op, long long lhs, long long rhs)
{
    switch (op)
    {
        case BinaryExpr::Op::Eq:
            return lhs == rhs;
        case BinaryExpr::Op::Ne:
            return lhs != rhs;
        case BinaryExpr::Op::Lt:
            return lhs < rhs;
        case BinaryExpr::Op::Le:
            return lhs <= rhs;
        case BinaryExpr::Op::Gt:
            return lhs > rhs;
        case BinaryExpr::Op::Ge:
            return lhs >= rhs;
        default:
            return false;
    }
}

} // namespace

/// @brief Fold a binary arithmetic expression when both operands are numeric literals.
///
/// Delegates to @ref foldNumericBinary with a callback that applies the requested
/// operator via @ref tryFoldBinaryArith.  Keeping the dispatch in one place makes
/// it easy for callers to produce literal replacements without duplicating
/// numeric promotion logic.
///
/// @param l Left-hand operand.
/// @param op Arithmetic operator to evaluate.
/// @param r Right-hand operand.
/// @return Literal expression for the folded result, or nullptr on failure.
ExprPtr foldBinaryArith(const Expr &l, BinaryExpr::Op op, const Expr &r)
{
    return foldNumericBinary(
        l,
        r,
        [op](const Numeric &lhs, const Numeric &rhs) -> std::optional<Numeric>
        {
            return tryFoldBinaryArith(lhs, op, rhs);
        });
}

/// @brief Fold a unary arithmetic expression when the operand is numeric.
///
/// Supports unary plus and minus by either forwarding the operand unchanged or
/// negating it using floating-point negation/wrap-around subtraction depending
/// on the operand type.
///
/// @param op Unary operator kind.
/// @param v Operand expression to inspect.
/// @return Literal expression on success or nullptr when folding is not possible.
ExprPtr foldUnaryArith(UnaryExpr::Op op, const Expr &v)
{
    if (auto value = asNumeric(v))
    {
        if (auto folded = tryFoldUnaryArith(op, *value))
        {
            if (folded->isFloat)
            {
                auto out = std::make_unique<FloatExpr>();
                out->value = folded->f;
                return out;
            }
            auto out = std::make_unique<IntExpr>();
            out->value = folded->i;
            return out;
        }
    }
    return nullptr;
}

/// @brief Fold a numeric comparison when operands are literals.
///
/// Relies on @ref tryFoldCompare to produce the 0/1 integer literal that
/// represents the boolean outcome while respecting the caller's request to allow
/// or disallow floating-point participation.
///
/// @param l Left-hand operand.
/// @param op Comparison operator to evaluate.
/// @param r Right-hand operand.
/// @param allowFloat Whether floating-point operands may participate.
/// @return Literal integer representing the comparison or nullptr.
ExprPtr foldCompare(const Expr &l, BinaryExpr::Op op, const Expr &r, bool allowFloat)
{
    return foldNumericBinary(
        l,
        r,
        [op, allowFloat](const Numeric &lhs, const Numeric &rhs) -> std::optional<Numeric>
        {
            return tryFoldCompare(lhs, op, rhs, allowFloat);
        });
}

/// @brief Dispatch to an operator-specific folding helper.
///
/// Keeps the per-operator arithmetic in dedicated helpers while exposing a
/// simple interface for @ref foldBinaryArith to invoke.
///
/// @param lhsRaw Left-hand operand prior to promotion.
/// @param op Arithmetic operator to execute.
/// @param rhsRaw Right-hand operand prior to promotion.
/// @return Folded numeric result or std::nullopt when folding is unsupported.
std::optional<Numeric> tryFoldBinaryArith(const Numeric &lhsRaw, BinaryExpr::Op op, const Numeric &rhsRaw)
{
    switch (op)
    {
        case BinaryExpr::Op::Add:
            return foldAdd(lhsRaw, rhsRaw);
        case BinaryExpr::Op::Sub:
            return foldSub(lhsRaw, rhsRaw);
        case BinaryExpr::Op::Mul:
            return foldMul(lhsRaw, rhsRaw);
        case BinaryExpr::Op::Div:
            return foldDiv(lhsRaw, rhsRaw);
        case BinaryExpr::Op::IDiv:
            return foldIDiv(lhsRaw, rhsRaw);
        case BinaryExpr::Op::Mod:
            return foldMod(lhsRaw, rhsRaw);
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Evaluate a unary arithmetic operator on a numeric literal.
///
/// Supports unary plus (no-op) and unary minus (negation).  Integer negation
/// uses @ref wrapSub to maintain wrap-around semantics.
///
/// @param op Unary operator to apply.
/// @param value Operand already promoted by the caller.
/// @return Folded value or std::nullopt for unsupported operators.
std::optional<Numeric> tryFoldUnaryArith(UnaryExpr::Op op, const Numeric &value)
{
    switch (op)
    {
        case UnaryExpr::Op::Plus:
            return value;
        case UnaryExpr::Op::Negate:
            if (value.isFloat)
            {
                double neg = -value.f;
                return Numeric{true, neg, static_cast<long long>(neg)};
            }
            else
            {
                long long neg = wrapSub(0, value.i);
                return Numeric{false, static_cast<double>(neg), neg};
            }
        default:
            break;
    }
    return std::nullopt;
}

/// @brief Evaluate a comparison between two promoted numeric literals.
///
/// Applies BASIC promotion rules, optionally forbidding floating-point
/// participation, and converts the boolean result into a Numeric with integer
/// payload 0/1.
///
/// @param lhsRaw Left-hand operand prior to promotion.
/// @param op Comparison operator.
/// @param rhsRaw Right-hand operand prior to promotion.
/// @param allowFloat Whether floating-point operands may be used.
/// @return Numeric representing the comparison result or std::nullopt.
std::optional<Numeric> tryFoldCompare(
    const Numeric &lhsRaw, BinaryExpr::Op op, const Numeric &rhsRaw, bool allowFloat)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (!allowFloat && (lhs.isFloat || rhs.isFloat))
        return std::nullopt;

    bool result = false;
    if (lhs.isFloat || rhs.isFloat)
    {
        double lv = lhs.isFloat ? lhs.f : static_cast<double>(lhs.i);
        double rv = rhs.isFloat ? rhs.f : static_cast<double>(rhs.i);
        result = compareFloat(op, lv, rv);
    }
    else
    {
        result = compareInt(op, lhs.i, rhs.i);
    }

    long long v = result ? 1 : 0;
    return Numeric{false, static_cast<double>(v), v};
}

/// @brief Fold addition for arbitrary numeric expressions.
ExprPtr foldNumericAdd(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Add, r);
}

/// @brief Fold subtraction for arbitrary numeric expressions.
ExprPtr foldNumericSub(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Sub, r);
}

/// @brief Fold multiplication for arbitrary numeric expressions.
ExprPtr foldNumericMul(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Mul, r);
}

/// @brief Fold floating-point division for arbitrary numeric expressions.
ExprPtr foldNumericDiv(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Div, r);
}

/// @brief Fold integer division for arbitrary numeric expressions.
ExprPtr foldNumericIDiv(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::IDiv, r);
}

/// @brief Fold modulo for arbitrary numeric expressions.
ExprPtr foldNumericMod(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Mod, r);
}

/// @brief Fold equality comparison for numeric expressions.
ExprPtr foldNumericEq(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Eq, r);
}

/// @brief Fold inequality comparison for numeric expressions.
ExprPtr foldNumericNe(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Ne, r);
}

/// @brief Fold less-than comparison for numeric expressions.
ExprPtr foldNumericLt(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Lt, r);
}

/// @brief Fold less-than-or-equal comparison for numeric expressions.
ExprPtr foldNumericLe(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Le, r);
}

/// @brief Fold greater-than comparison for numeric expressions.
ExprPtr foldNumericGt(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Gt, r);
}

/// @brief Fold greater-than-or-equal comparison for numeric expressions.
ExprPtr foldNumericGe(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Ge, r);
}

/// @brief Fold bitwise AND for numeric expressions interpreted as booleans.
ExprPtr foldNumericAnd(const Expr &l, const Expr &r)
{
    return foldNumericBinary(
        l,
        r,
        [](const Numeric &lhs, const Numeric &rhs) -> std::optional<Numeric>
        {
            if (lhs.isFloat || rhs.isFloat)
                return std::nullopt;
            bool result = (lhs.i != 0) && (rhs.i != 0);
            long long v = result ? 1 : 0;
            return Numeric{false, static_cast<double>(v), v};
        });
}

/// @brief Fold bitwise OR for numeric expressions interpreted as booleans.
ExprPtr foldNumericOr(const Expr &l, const Expr &r)
{
    return foldNumericBinary(
        l,
        r,
        [](const Numeric &lhs, const Numeric &rhs) -> std::optional<Numeric>
        {
            if (lhs.isFloat || rhs.isFloat)
                return std::nullopt;
            bool result = (lhs.i != 0) || (rhs.i != 0);
            long long v = result ? 1 : 0;
            return Numeric{false, static_cast<double>(v), v};
        });
}

} // namespace il::frontends::basic::detail

