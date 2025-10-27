//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// String constant folding helpers for the BASIC front end.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements string folding utilities for operators and builtins.

#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ast/ExprNodes.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace il::frontends::basic::constfold
{
namespace
{
const ::il::frontends::basic::StringExpr *as_string(const AST::Expr &expr)
{
    return dynamic_cast<const ::il::frontends::basic::StringExpr *>(&expr);
}

std::optional<long long> as_index(const AST::Expr &expr)
{
    auto numeric = numeric_from_expr(expr);
    if (!numeric || numeric->isFloat)
        return std::nullopt;
    return numeric->i;
}

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

AST::ExprPtr make_string(std::string value)
{
    auto out = std::make_unique<::il::frontends::basic::StringExpr>();
    out->value = std::move(value);
    return out;
}

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

AST::ExprPtr foldLenLiteral(const AST::Expr &arg)
{
    if (auto *s = as_string(arg))
        return make_length(s->value.size());
    return nullptr;
}

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

} // namespace il::frontends::basic::constfold
