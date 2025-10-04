// File: src/frontends/basic/ConstFold_Arith.cpp
// Purpose: Implements arithmetic constant folding utilities for BASIC expressions.
// Key invariants: Numeric folding honors BASIC promotion rules and 64-bit wrap-around semantics.
// Ownership/Lifetime: Returned expressions are heap-allocated and owned by callers.
// Links: docs/codemap.md

#include "frontends/basic/ConstFold_Arith.hpp"

#include "frontends/basic/ConstFoldHelpers.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace il::frontends::basic::detail
{
namespace
{
long long wrapAdd(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

long long wrapSub(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}

long long wrapMul(long long a, long long b)
{
    return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

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

std::optional<Numeric> foldIDiv(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (lhs.isFloat || rhs.isFloat || rhs.i == 0)
        return std::nullopt;

    long long v = lhs.i / rhs.i;
    return Numeric{false, static_cast<double>(v), v};
}

std::optional<Numeric> foldMod(const Numeric &lhsRaw, const Numeric &rhsRaw)
{
    Numeric lhs = promote(lhsRaw, rhsRaw);
    Numeric rhs = promote(rhsRaw, lhsRaw);

    if (lhs.isFloat || rhs.isFloat || rhs.i == 0)
        return std::nullopt;

    long long v = lhs.i % rhs.i;
    return Numeric{false, static_cast<double>(v), v};
}

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

std::optional<Numeric> tryFoldUnaryArith(UnaryExpr::Op op, const Numeric &value)
{
    switch (op)
    {
        default:
            break;
    }
    return std::nullopt;
}

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

ExprPtr foldNumericAdd(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Add, r);
}

ExprPtr foldNumericSub(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Sub, r);
}

ExprPtr foldNumericMul(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Mul, r);
}

ExprPtr foldNumericDiv(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Div, r);
}

ExprPtr foldNumericIDiv(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::IDiv, r);
}

ExprPtr foldNumericMod(const Expr &l, const Expr &r)
{
    return foldBinaryArith(l, BinaryExpr::Op::Mod, r);
}

ExprPtr foldNumericEq(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Eq, r);
}

ExprPtr foldNumericNe(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Ne, r);
}

ExprPtr foldNumericLt(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Lt, r);
}

ExprPtr foldNumericLe(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Le, r);
}

ExprPtr foldNumericGt(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Gt, r);
}

ExprPtr foldNumericGe(const Expr &l, const Expr &r)
{
    return foldCompare(l, BinaryExpr::Op::Ge, r);
}

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

