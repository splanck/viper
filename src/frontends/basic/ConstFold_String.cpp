// File: src/frontends/basic/ConstFold_String.cpp
// Purpose: Implements string constant folding utilities for BASIC expressions.
// Key invariants: Helpers clamp indices to valid ranges and gracefully handle
//                 empty and out-of-range slices.
// Ownership/Lifetime: Returned expressions are heap-allocated and owned by
//                     callers.
// Links: docs/codemap.md

#include "frontends/basic/ConstFold_String.hpp"

#include "frontends/basic/ConstFoldHelpers.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace il::frontends::basic::detail
{
namespace
{
ExprPtr makeString(std::string value)
{
    auto out = std::make_unique<StringExpr>();
    out->value = std::move(value);
    return out;
}

ExprPtr makeLength(std::size_t length)
{
    auto out = std::make_unique<IntExpr>();
    if (length > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
        out->value = std::numeric_limits<long long>::max();
    else
        out->value = static_cast<long long>(length);
    return out;
}

std::optional<std::string> literalValue(const Expr &expr)
{
    if (const auto *stringExpr = dynamic_cast<const StringExpr *>(&expr))
        return stringExpr->value;
    return std::nullopt;
}

std::optional<long long> literalIndex(const Expr &expr)
{
    if (auto numeric = asNumeric(expr))
    {
        if (numeric->isFloat)
            return std::nullopt;
        return numeric->i;
    }
    return std::nullopt;
}

std::size_t clampCount(long long count, std::size_t limit)
{
    if (count <= 0)
        return 0;
    auto asUnsigned = static_cast<unsigned long long>(count);
    auto maxUnsigned = static_cast<unsigned long long>(limit);
    if (asUnsigned >= maxUnsigned)
        return limit;
    return static_cast<std::size_t>(asUnsigned);
}
} // namespace

ExprPtr foldStringConcat(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr {
        return makeString(a + b);
    });
}

ExprPtr foldStringEq(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr {
        auto out = std::make_unique<IntExpr>();
        out->value = (a == b) ? 1 : 0;
        return out;
    });
}

ExprPtr foldStringNe(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr {
        auto out = std::make_unique<IntExpr>();
        out->value = (a != b) ? 1 : 0;
        return out;
    });
}

ExprPtr foldLenLiteral(const Expr &arg)
{
    auto value = literalValue(arg);
    if (!value)
        return nullptr;
    return makeLength(value->size());
}

ExprPtr foldMidLiteral(const Expr &source, const Expr &startExpr, const Expr &lengthExpr)
{
    auto value = literalValue(source);
    auto start = literalIndex(startExpr);
    auto length = literalIndex(lengthExpr);
    if (!value || !start || !length)
        return nullptr;

    if (*length <= 0 || value->empty())
        return makeString("");

    long long oneBasedStart = std::max<long long>(*start, 1);
    if (oneBasedStart > static_cast<long long>(value->size()))
        return makeString("");

    std::size_t startIndex = static_cast<std::size_t>(oneBasedStart - 1);
    std::size_t available = value->size() - startIndex;
    std::size_t slice = clampCount(*length, available);
    return makeString(value->substr(startIndex, slice));
}

ExprPtr foldLeftLiteral(const Expr &source, const Expr &countExpr)
{
    auto value = literalValue(source);
    auto count = literalIndex(countExpr);
    if (!value || !count)
        return nullptr;

    if (*count <= 0 || value->empty())
        return makeString("");

    std::size_t take = clampCount(*count, value->size());
    return makeString(value->substr(0, take));
}

ExprPtr foldRightLiteral(const Expr &source, const Expr &countExpr)
{
    auto value = literalValue(source);
    auto count = literalIndex(countExpr);
    if (!value || !count)
        return nullptr;

    if (*count <= 0 || value->empty())
        return makeString("");

    std::size_t take = clampCount(*count, value->size());
    std::size_t start = value->size() - take;
    return makeString(value->substr(start, take));
}

} // namespace il::frontends::basic::detail
