//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// This source file is part of the Viper project.
//
// File: src/frontends/basic/constfold/FoldStrings.cpp
// Purpose: Provide the BASIC constant folder with routines that collapse string
//          expressions, enabling operator and builtin evaluation at parse time
//          when arguments are literal.
// Key invariants: Enforces BASIC's 1-based indexing rules for slicing helpers,
//                 clamps counts to avoid overflow, and ensures folded nodes use
//                 the canonical AST types.
// Ownership/Lifetime: Produces new AST nodes wrapped in unique_ptr instances;
//                     ownership transfers to callers that splice them into the
//                     AST.
// Links: docs/codemap.md, docs/il-guide.md#basic-frontend-constant-folding
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements string folding utilities for operators and builtins.
/// @details Covers concatenation as well as LEN/MID$/LEFT$/RIGHT$ literal
///          evaluations so tooling can simplify common idioms before lowering to
///          IL.

#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ASTUtils.hpp"
#include "frontends/basic/ast/ExprNodes.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace il::frontends::basic::constfold
{
namespace
{
/// @brief Cast an expression to a string literal node when possible.
/// @param expr Expression to inspect.
/// @return Pointer to the string expression or @c nullptr when the cast fails.
const StringExpr *as_string(const AST::Expr &expr)
{
    return as<const StringExpr>(expr);
}

/// @brief Extract an integer index from a literal expression.
/// @details Converts integer literals to their raw value and rejects floating
///          point representations so slicing helpers respect BASIC semantics.
/// @param expr Expression to interpret as an index.
/// @return Parsed index when available.
std::optional<long long> as_index(const AST::Expr &expr)
{
    auto numeric = numeric_from_expr(expr);
    if (!numeric || numeric->isFloat)
        return std::nullopt;
    return numeric->i;
}

/// @brief Clamp a requested substring length to the available characters.
/// @details Treats zero and negative inputs as zero, ensuring fold helpers
///          produce empty strings rather than triggering UB. Large values cap at
///          @p limit to mirror runtime behaviour.
/// @param count Requested number of characters.
/// @param limit Maximum characters that may be consumed.
/// @return Safe count within the range [0, limit].
std::size_t clamp_count(long long count, std::size_t limit)
{
    if (count <= 0)
        return 0;
    auto capped = static_cast<unsigned long long>(count);
    auto maxVal = static_cast<unsigned long long>(limit);
    if (capped >= maxVal)
        return limit;
    return static_cast<std::size_t>(capped);
}

/// @brief Create a new string literal expression node.
/// @param value Text to embed within the AST node.
/// @return Unique pointer owning the newly created string literal.
AST::ExprPtr make_string(std::string value)
{
    auto out = std::make_unique<::il::frontends::basic::StringExpr>();
    out->value = std::move(value);
    return out;
}

/// @brief Create an integer literal node representing a string length.
/// @details Caps the encoded value at `LLONG_MAX` to avoid overflow when the
///          length exceeds the integer range.
/// @param length Character count to encode.
/// @return Unique pointer owning the integer literal node.
AST::ExprPtr make_length(std::size_t length)
{
    auto out = std::make_unique<::il::frontends::basic::IntExpr>();
    if (length > static_cast<std::size_t>(std::numeric_limits<long long>::max()))
        out->value = std::numeric_limits<long long>::max();
    else
        out->value = static_cast<long long>(length);
    return out;
}

} // namespace

/// @brief Fold string binary operators when both operands are literals.
/// @details Currently handles concatenation by joining the two literal payloads
///          into a new string constant, mirroring BASIC's `+` operator.
/// @param op Binary operator under consideration.
/// @param lhs Left-hand string constant.
/// @param rhs Right-hand string constant.
/// @return Folded string constant or @c std::nullopt when folding is unsupported.
std::optional<Constant> fold_strings(AST::BinaryExpr::Op op,
                                     const Constant &lhs,
                                     const Constant &rhs)
{
    if (op != AST::BinaryExpr::Op::Add)
        return std::nullopt;
    if (lhs.kind != LiteralKind::String || rhs.kind != LiteralKind::String)
        return std::nullopt;
    Constant c;
    c.kind = LiteralKind::String;
    c.stringValue = lhs.stringValue + rhs.stringValue;
    return c;
}

/// @brief Fold a LEN() invocation when the argument is a string literal.
/// @param arg Expression passed to LEN().
/// @return Integer literal describing the string length, or @c nullptr when the
///         argument is not literal.
AST::ExprPtr foldLenLiteral(const AST::Expr &arg)
{
    if (auto *s = as_string(arg))
        return make_length(s->value.size());
    return nullptr;
}

/// @brief Fold a MID$ literal slice when all arguments are literals.
/// @details Applies BASIC's 1-based indexing, gracefully handles indices beyond
///          the source length, and respects requested lengths that exceed the
///          remainder of the string.
/// @param source Source string expression.
/// @param startExpr Expression describing the 1-based start index.
/// @param lengthExpr Expression describing the requested slice length.
/// @return String literal capturing the sliced substring or @c nullptr.
AST::ExprPtr foldMidLiteral(const AST::Expr &source,
                            const AST::Expr &startExpr,
                            const AST::Expr &lengthExpr)
{
    auto *s = as_string(source);
    auto start = as_index(startExpr);
    auto length = as_index(lengthExpr);
    if (!s || !start || !length)
        return nullptr;
    if (*length <= 0 || s->value.empty())
        return make_string("");
    long long begin = std::max<long long>(*start, 1);
    if (begin > static_cast<long long>(s->value.size()))
        return make_string("");
    std::size_t startIndex = static_cast<std::size_t>(begin - 1);
    std::size_t available = s->value.size() - startIndex;
    std::size_t take = clamp_count(*length, available);
    return make_string(s->value.substr(startIndex, take));
}

/// @brief Fold a LEFT$ invocation with literal arguments.
/// @details Returns the requested prefix, applying @ref clamp_count to mirror
///          runtime behaviour when the count exceeds the string length.
/// @param source Source string expression.
/// @param countExpr Expression describing the number of characters to keep.
/// @return Folded string literal or @c nullptr when inputs are not literal.
AST::ExprPtr foldLeftLiteral(const AST::Expr &source, const AST::Expr &countExpr)
{
    auto *s = as_string(source);
    auto count = as_index(countExpr);
    if (!s || !count)
        return nullptr;
    if (*count <= 0 || s->value.empty())
        return make_string("");
    std::size_t take = clamp_count(*count, s->value.size());
    return make_string(s->value.substr(0, take));
}

/// @brief Fold a RIGHT$ invocation with literal arguments.
/// @details Produces the requested suffix, ensuring counts outside the valid
///          range return empty strings and clamping large requests to the source
///          length.
/// @param source Source string expression.
/// @param countExpr Expression describing the number of characters to take from
///                  the end.
/// @return Folded string literal or @c nullptr when folding cannot proceed.
AST::ExprPtr foldRightLiteral(const AST::Expr &source, const AST::Expr &countExpr)
{
    auto *s = as_string(source);
    auto count = as_index(countExpr);
    if (!s || !count)
        return nullptr;
    if (*count <= 0 || s->value.empty())
        return make_string("");
    std::size_t take = clamp_count(*count, s->value.size());
    std::size_t start = s->value.size() - take;
    return make_string(s->value.substr(start, take));
}

/// @brief Fold a CHR$() invocation when the argument is a literal integer.
/// @details Converts an integer literal in the range [0, 255] into a single-
///          character string. Values outside this range are rejected to maintain
///          BASIC's ASCII/extended ASCII semantics.
/// @param arg Expression passed to CHR$().
/// @return String literal containing the character, or @c nullptr when the
///         argument is not a valid literal integer in range.
AST::ExprPtr foldChrLiteral(const AST::Expr &arg)
{
    auto charCode = as_index(arg);
    if (!charCode)
        return nullptr;
    // Accept values in [0, 255] for standard ASCII/extended ASCII range
    if (*charCode < 0 || *charCode > 255)
        return nullptr;
    char ch = static_cast<char>(*charCode);
    return make_string(std::string(1, ch));
}

} // namespace il::frontends::basic::constfold
