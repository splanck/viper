//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Dispatches BASIC constant folding requests to domain-specific helpers.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the central dispatch logic for constant folding.

#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ast/ExprNodes.hpp"

#include <cassert>
#include <optional>

namespace il::frontends::basic::constfold
{
std::optional<Constant> fold_arith(AST::BinaryExpr::Op, const Constant &, const Constant &);
std::optional<Constant> fold_numeric_logic(AST::BinaryExpr::Op, const Constant &, const Constant &);
std::optional<Constant> fold_compare(AST::BinaryExpr::Op, const Constant &, const Constant &);
std::optional<Constant> fold_strings(AST::BinaryExpr::Op, const Constant &, const Constant &);
std::optional<Constant> fold_cast(AST::BinaryExpr::Op, const Constant &, const Constant &);

namespace
{
bool is_numeric(LiteralKind kind)
{
    return kind == LiteralKind::Int || kind == LiteralKind::Float;
}

} // namespace

std::optional<NumericValue> numeric_from_expr(const AST::Expr &expr)
{
    if (auto *i = dynamic_cast<const ::il::frontends::basic::IntExpr *>(&expr))
        return NumericValue{false, static_cast<double>(i->value), i->value};
    if (auto *f = dynamic_cast<const ::il::frontends::basic::FloatExpr *>(&expr))
        return NumericValue{true, f->value, static_cast<long long>(f->value)};
    return std::nullopt;
}

NumericValue promote_numeric(const NumericValue &lhs, const NumericValue &rhs)
{
    if (lhs.isFloat || rhs.isFloat)
        return NumericValue{true, lhs.isFloat ? lhs.f : static_cast<double>(lhs.i), lhs.i};
    return lhs;
}

namespace
{

std::optional<Constant> extract_constant(const AST::Expr &expr)
{
    if (auto *i = dynamic_cast<const ::il::frontends::basic::IntExpr *>(&expr))
    {
        Constant c;
        c.kind = LiteralKind::Int;
        c.numeric = NumericValue{false, static_cast<double>(i->value), i->value};
        return c;
    }
    if (auto *f = dynamic_cast<const ::il::frontends::basic::FloatExpr *>(&expr))
    {
        Constant c;
        c.kind = LiteralKind::Float;
        c.numeric = NumericValue{true, f->value, static_cast<long long>(f->value)};
        return c;
    }
    if (auto *b = dynamic_cast<const ::il::frontends::basic::BoolExpr *>(&expr))
    {
        Constant c;
        c.kind = LiteralKind::Bool;
        c.boolValue = b->value;
        c.numeric = NumericValue{false, b->value ? 1.0 : 0.0, b->value ? 1 : 0};
        return c;
    }
    if (auto *s = dynamic_cast<const ::il::frontends::basic::StringExpr *>(&expr))
    {
        Constant c;
        c.kind = LiteralKind::String;
        c.stringValue = s->value;
        return c;
    }
    return std::nullopt;
}

AST::ExprPtr materialize_constant(const Constant &constant)
{
    switch (constant.kind)
    {
        case LiteralKind::Int:
        {
            auto out = std::make_unique<::il::frontends::basic::IntExpr>();
            out->value = constant.numeric.i;
            return out;
        }
        case LiteralKind::Float:
        {
            auto out = std::make_unique<::il::frontends::basic::FloatExpr>();
            out->value = constant.numeric.isFloat ? constant.numeric.f
                                                  : static_cast<double>(constant.numeric.i);
            return out;
        }
        case LiteralKind::Bool:
        {
            auto out = std::make_unique<::il::frontends::basic::BoolExpr>();
            out->value = constant.boolValue;
            return out;
        }
        case LiteralKind::String:
        {
            auto out = std::make_unique<::il::frontends::basic::StringExpr>();
            out->value = constant.stringValue;
            return out;
        }
        case LiteralKind::Invalid:
            break;
    }
    return nullptr;
}

std::optional<FoldKind> deduce_kind(AST::BinaryExpr::Op op, LiteralKind lhs, LiteralKind rhs)
{
    switch (op)
    {
        case AST::BinaryExpr::Op::Add:
            if (lhs == LiteralKind::String && rhs == LiteralKind::String)
                return FoldKind::Strings;
            if (is_numeric(lhs) && is_numeric(rhs))
                return FoldKind::Arith;
            return std::nullopt;
        case AST::BinaryExpr::Op::Sub:
        case AST::BinaryExpr::Op::Mul:
        case AST::BinaryExpr::Op::Div:
        case AST::BinaryExpr::Op::IDiv:
        case AST::BinaryExpr::Op::Mod:
            return (is_numeric(lhs) && is_numeric(rhs)) ? std::optional<FoldKind>(FoldKind::Arith)
                                                        : std::nullopt;
        case AST::BinaryExpr::Op::LogicalAnd:
        case AST::BinaryExpr::Op::LogicalAndShort:
        case AST::BinaryExpr::Op::LogicalOr:
        case AST::BinaryExpr::Op::LogicalOrShort:
            return FoldKind::Logical;
        case AST::BinaryExpr::Op::Eq:
        case AST::BinaryExpr::Op::Ne:
        case AST::BinaryExpr::Op::Lt:
        case AST::BinaryExpr::Op::Le:
        case AST::BinaryExpr::Op::Gt:
        case AST::BinaryExpr::Op::Ge:
            return FoldKind::Compare;
        default:
            return std::nullopt;
    }
}

#ifdef VIPER_CONSTFOLD_ASSERTS
bool same_constant(const Constant &lhs, const Constant &rhs)
{
    if (lhs.kind != rhs.kind)
        return false;
    switch (lhs.kind)
    {
        case LiteralKind::Int:
        case LiteralKind::Float:
            return lhs.numeric.isFloat == rhs.numeric.isFloat && lhs.numeric.i == rhs.numeric.i &&
                   lhs.numeric.f == rhs.numeric.f;
        case LiteralKind::Bool:
            return lhs.boolValue == rhs.boolValue;
        case LiteralKind::String:
            return lhs.stringValue == rhs.stringValue;
        case LiteralKind::Invalid:
            return true;
    }
    return false;
}
#endif

std::optional<Constant> dispatch_fold(FoldKind kind,
                                      AST::BinaryExpr::Op op,
                                      const Constant &lhs,
                                      const Constant &rhs)
{
    switch (kind)
    {
        case FoldKind::Arith:
            return fold_arith(op, lhs, rhs);
        case FoldKind::Logical:
            return fold_numeric_logic(op, lhs, rhs);
        case FoldKind::Compare:
            return fold_compare(op, lhs, rhs);
        case FoldKind::Strings:
            return fold_strings(op, lhs, rhs);
        case FoldKind::Casts:
            return fold_cast(op, lhs, rhs);
    }
    return std::nullopt;
}

} // namespace

bool can_fold(const AST::Expr &expr)
{
    auto *binary = dynamic_cast<const ::il::frontends::basic::BinaryExpr *>(&expr);
    if (!binary)
        return false;
    auto lhs = extract_constant(*binary->lhs);
    auto rhs = extract_constant(*binary->rhs);
    if (!lhs || !rhs)
        return false;
    auto kind = deduce_kind(binary->op, lhs->kind, rhs->kind);
    if (!kind)
        return false;
    return dispatch_fold(*kind, binary->op, *lhs, *rhs).has_value();
}

std::optional<AST::ExprPtr> fold_expr(const AST::Expr &expr)
{
    auto *binary = dynamic_cast<const ::il::frontends::basic::BinaryExpr *>(&expr);
    if (!binary)
        return std::nullopt;

    auto lhs = extract_constant(*binary->lhs);
    auto rhs = extract_constant(*binary->rhs);
    if (!lhs || !rhs)
        return std::nullopt;

    auto kind = deduce_kind(binary->op, lhs->kind, rhs->kind);
    if (!kind)
        return std::nullopt;

    auto folded = dispatch_fold(*kind, binary->op, *lhs, *rhs);
#ifdef VIPER_CONSTFOLD_ASSERTS
    if (folded)
    {
        if (*kind == FoldKind::Arith &&
            (binary->op == AST::BinaryExpr::Op::Add || binary->op == AST::BinaryExpr::Op::Mul))
        {
            auto swapped = dispatch_fold(*kind, binary->op, *rhs, *lhs);
            if (swapped)
                assert(same_constant(*folded, *swapped));
        }
        if (*kind == FoldKind::Logical && (binary->op == AST::BinaryExpr::Op::LogicalAnd ||
                                           binary->op == AST::BinaryExpr::Op::LogicalAndShort ||
                                           binary->op == AST::BinaryExpr::Op::LogicalOr ||
                                           binary->op == AST::BinaryExpr::Op::LogicalOrShort))
        {
            auto swapped = dispatch_fold(*kind, binary->op, *rhs, *lhs);
            if (swapped)
                assert(same_constant(*folded, *swapped));
        }
    }
#endif
    if (!folded)
        return std::nullopt;
    return materialize_constant(*folded);
}

} // namespace il::frontends::basic::constfold
