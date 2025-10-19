// File: src/frontends/basic/ConstFold_String.cpp
// License: MIT License. See LICENSE in the project root for full license
//          information.
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
template <typename Fn>
ExprPtr dispatchStringBinary(const Expr &lhs, const Expr &rhs, Fn fn)
{
    const auto *left = dynamic_cast<const StringExpr *>(&lhs);
    const auto *right = dynamic_cast<const StringExpr *>(&rhs);
    if (!left || !right)
        return nullptr;
    return fn(*left, *right);
}

/// @brief Construct a string literal node that adopts @p value.
///
/// The helper centralizes allocation of @ref StringExpr nodes so all folding
/// routines consistently move the computed string into a new AST literal.
///
/// @param value Text transferred into the literal expression.
/// @return Newly created string literal.
ExprPtr makeString(std::string value)
{
    auto out = std::make_unique<StringExpr>();
    out->value = std::move(value);
    return out;
}

/// @brief Create an integer literal representing a string length.
///
/// BASIC string lengths must fit into a signed 64-bit integer.  When the
/// computed length exceeds that limit the helper saturates to the maximum
/// representable value to mirror runtime behaviour.
///
/// @param length Length value to encode.
/// @return Integer literal node containing the possibly clamped length.
ExprPtr makeLength(std::size_t length)
{
    auto out = std::make_unique<IntExpr>();
    if (length > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
        out->value = std::numeric_limits<long long>::max();
    else
        out->value = static_cast<long long>(length);
    return out;
}

/// @brief Extract the textual payload from a string literal expression.
///
/// @param expr Expression inspected for literal content.
/// @return Stored string when @p expr is a StringExpr; otherwise std::nullopt.
std::optional<std::string> literalValue(const Expr &expr)
{
    if (const auto *stringExpr = dynamic_cast<const StringExpr *>(&expr))
        return stringExpr->value;
    return std::nullopt;
}

/// @brief Interpret an expression as an integer literal index.
///
/// The helper rejects floating-point literals so that callers only receive
/// indices that correspond to BASIC's integer semantics.
///
/// @param expr Expression inspected for an integer literal.
/// @return Index value when @p expr is an integer literal; otherwise empty.
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

/// @brief Clamp a requested slice count to the valid string range.
///
/// Negative or zero counts become zero, while counts larger than @p limit are
/// capped to prevent over-reading the literal value.
///
/// @param count Requested length expressed as a signed integer.
/// @param limit Maximum characters available from the source string.
/// @return Safe count that respects the available slice size.
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

/// @brief Fold concatenation of two string literals.
///
/// Relies on the shared @ref foldString helper to compute the result when both
/// operands are string literals.  The lambda simply concatenates the literal
/// text using standard string addition.
///
/// @param l Left-hand operand expected to be a string literal.
/// @param r Right-hand operand expected to be a string literal.
/// @return Concatenated literal or nullptr when folding is not possible.
ExprPtr foldStringConcat(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr {
        return makeString(a + b);
    });
}

/// @brief Fold string equality comparison between two literals.
///
/// Emits an integer literal containing 1 when the values match or 0 otherwise.
/// The return type mirrors BASIC's convention of representing booleans as
/// integers.
///
/// @param l Left-hand string literal.
/// @param r Right-hand string literal.
/// @return Integer literal encoding the comparison result, or nullptr.
ExprPtr foldStringEq(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr {
        auto out = std::make_unique<IntExpr>();
        out->value = (a == b) ? 1 : 0;
        return out;
    });
}

/// @brief Fold string inequality comparison between two literals.
///
/// Returns 1 when the operands differ and 0 otherwise, matching BASIC's
/// integer-based boolean representation.
///
/// @param l Left-hand string literal.
/// @param r Right-hand string literal.
/// @return Integer literal encoding the comparison result, or nullptr.
ExprPtr foldStringNe(const StringExpr &l, const StringExpr &r)
{
    return foldString(l, r, [](const std::string &a, const std::string &b) -> ExprPtr {
        auto out = std::make_unique<IntExpr>();
        out->value = (a != b) ? 1 : 0;
        return out;
    });
}

ExprPtr foldStringBinaryConcat(const Expr &lhs, const Expr &rhs)
{
    return dispatchStringBinary(lhs, rhs, foldStringConcat);
}

ExprPtr foldStringBinaryEq(const Expr &lhs, const Expr &rhs)
{
    return dispatchStringBinary(lhs, rhs, foldStringEq);
}

ExprPtr foldStringBinaryNe(const Expr &lhs, const Expr &rhs)
{
    return dispatchStringBinary(lhs, rhs, foldStringNe);
}

/// @brief Fold LEN applied to a string literal.
///
/// Computes the literal length using @ref makeLength so overly long strings are
/// automatically clamped to the runtime limit.
///
/// @param arg Expression supplied to LEN.
/// @return Integer literal containing the length, or nullptr when @p arg is not
///         a string literal.
ExprPtr foldLenLiteral(const Expr &arg)
{
    auto value = literalValue(arg);
    if (!value)
        return nullptr;
    return makeLength(value->size());
}

/// @brief Fold MID$ when all operands are literals.
///
/// Applies BASIC's one-based indexing and clamps the requested length to the
/// available slice.  When the start index is out of range or the length is
/// non-positive an empty string literal is produced.
///
/// @param source Source string expression.
/// @param startExpr Literal expression specifying the start index.
/// @param lengthExpr Literal expression specifying the length.
/// @return String literal containing the sliced segment, or nullptr on failure.
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

/// @brief Fold LEFT$ when both operands are literals.
///
/// LEFT$ extracts the first N characters of the string.  Negative counts or
/// empty strings yield an empty literal, otherwise the result is truncated using
/// @ref clampCount to avoid overruns.
///
/// @param source String expression to slice.
/// @param countExpr Expression providing the count literal.
/// @return Literal prefix or nullptr when folding is not possible.
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

/// @brief Fold RIGHT$ when both operands are literals.
///
/// RIGHT$ returns the last N characters of the string.  The helper clamps the
/// request to the string length and handles empty inputs gracefully.
///
/// @param source String expression to slice.
/// @param countExpr Expression providing the count literal.
/// @return Literal suffix or nullptr when folding is not possible.
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
