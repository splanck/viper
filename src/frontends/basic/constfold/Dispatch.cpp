//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the central dispatcher for BASIC constant folding.  The dispatcher
// maps a binary opcode and literal operand kinds to specialised folding
// routines, allowing each domain helper to stay compact while keeping lookup
// logic data-driven.
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/constfold/Dispatch.hpp"

#include "frontends/basic/ConstFolder.hpp"

#include <array>

namespace il::frontends::basic::constfold
{
namespace
{
using detail::Constant;
using detail::LiteralType;

struct DispatchEntry
{
    BinaryExpr::Op op;
    LiteralType lhs;
    LiteralType rhs;
    FoldKind kind;
    bool commutative;
};

constexpr std::array<DispatchEntry, 17> kDispatchTable{{
    {BinaryExpr::Op::Add, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Arith, true},
    {BinaryExpr::Op::Sub, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Arith, false},
    {BinaryExpr::Op::Mul, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Arith, true},
    {BinaryExpr::Op::Div, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Arith, false},
    {BinaryExpr::Op::IDiv, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Arith, false},
    {BinaryExpr::Op::Mod, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Arith, false},
    {BinaryExpr::Op::LogicalAndShort, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Logical, true},
    {BinaryExpr::Op::LogicalOrShort, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Logical, true},
    {BinaryExpr::Op::LogicalAnd, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Logical, true},
    {BinaryExpr::Op::LogicalOr, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Logical, true},
    {BinaryExpr::Op::Eq, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Compare, true},
    {BinaryExpr::Op::Ne, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Compare, true},
    {BinaryExpr::Op::Lt, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Compare, false},
    {BinaryExpr::Op::Le, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Compare, false},
    {BinaryExpr::Op::Gt, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Compare, false},
    {BinaryExpr::Op::Ge, LiteralType::Numeric, LiteralType::Numeric, FoldKind::Compare, false},
    {BinaryExpr::Op::Eq, LiteralType::String, LiteralType::String, FoldKind::Compare, true},
}};

constexpr std::array<DispatchEntry, 1> kStringDispatch{{
    {BinaryExpr::Op::Add, LiteralType::String, LiteralType::String, FoldKind::Strings, false},
}};

[[nodiscard]] bool matchesType(LiteralType want, LiteralType have)
{
    if (want == LiteralType::Numeric)
        return have == LiteralType::Int || have == LiteralType::Float;
    return want == have;
}

struct Match
{
    const DispatchEntry *entry{nullptr};
    bool swapped{false};
};

[[nodiscard]] std::optional<Match> findMatch(BinaryExpr::Op op, LiteralType lhs, LiteralType rhs)
{
    auto probe = [op, lhs, rhs](const DispatchEntry &entry) -> std::optional<Match>
    {
        if (entry.op != op)
            return std::nullopt;
        if (matchesType(entry.lhs, lhs) && matchesType(entry.rhs, rhs))
            return Match{&entry, false};
        if (entry.commutative && matchesType(entry.lhs, rhs) && matchesType(entry.rhs, lhs))
            return Match{&entry, true};
        return std::nullopt;
    };

    for (const auto &entry : kDispatchTable)
        if (auto match = probe(entry))
            return match;
    for (const auto &entry : kStringDispatch)
        if (auto match = probe(entry))
            return match;
    return std::nullopt;
}

[[nodiscard]] std::optional<Constant> applyKind(FoldKind kind,
                                                BinaryExpr::Op op,
                                                const Constant &lhs,
                                                const Constant &rhs)
{
    switch (kind)
    {
        case FoldKind::Arith:
            return detail::fold_arith(op, lhs, rhs);
        case FoldKind::Logical:
            return detail::fold_logical(op, lhs, rhs);
        case FoldKind::Compare:
            return detail::fold_compare(op, lhs, rhs);
        case FoldKind::Strings:
            return detail::fold_strings(op, lhs, rhs);
        case FoldKind::Casts:
            return detail::fold_casts(op, lhs, rhs);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Constant> extractConstant(const Expr &expr)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&expr))
    {
        auto value = detail::makeIntConstant(i->value);
        value.floatValue = static_cast<double>(i->value);
        return value;
    }
    if (auto *f = dynamic_cast<const FloatExpr *>(&expr))
    {
        auto value = detail::makeFloatConstant(f->value);
        return value;
    }
    if (auto *b = dynamic_cast<const BoolExpr *>(&expr))
    {
        auto value = detail::makeBoolConstant(b->value);
        return value;
    }
    if (auto *s = dynamic_cast<const StringExpr *>(&expr))
    {
        auto value = detail::makeStringConstant(s->value);
        return value;
    }
    return std::nullopt;
}

[[nodiscard]] ExprPtr materializeConstant(const Constant &constant)
{
    switch (constant.type)
    {
        case LiteralType::Int:
        {
            auto out = std::make_unique<IntExpr>();
            out->value = constant.intValue;
            return out;
        }
        case LiteralType::Float:
        {
            auto out = std::make_unique<FloatExpr>();
            out->value = constant.isFloat ? constant.floatValue
                                          : static_cast<double>(constant.intValue);
            return out;
        }
        case LiteralType::Bool:
        {
            auto out = std::make_unique<BoolExpr>();
            out->value = constant.boolValue;
            return out;
        }
        case LiteralType::String:
        {
            auto out = std::make_unique<StringExpr>();
            out->value = constant.stringValue;
            return out;
        }
        case LiteralType::Numeric:
            break;
    }
    return nullptr;
}

[[nodiscard]] std::optional<Constant> foldConstant(const Expr &expr)
{
    auto *binary = dynamic_cast<const BinaryExpr *>(&expr);
    if (!binary)
        return std::nullopt;
    if (!binary->lhs || !binary->rhs)
        return std::nullopt;

    auto lhsConst = extractConstant(*binary->lhs);
    auto rhsConst = extractConstant(*binary->rhs);
    if (!lhsConst || !rhsConst)
        return std::nullopt;

    auto match = findMatch(binary->op, lhsConst->type, rhsConst->type);
    if (!match)
        return std::nullopt;

    const Constant &first = match->swapped ? *rhsConst : *lhsConst;
    const Constant &second = match->swapped ? *lhsConst : *rhsConst;
    return applyKind(match->entry->kind, binary->op, first, second);
}

} // namespace

bool can_fold(const Expr &expr)
{
    return foldConstant(expr).has_value();
}

std::optional<ExprPtr> fold_expr(const Expr &expr)
{
    auto constant = foldConstant(expr);
    if (!constant)
        return std::nullopt;
    auto node = materializeConstant(*constant);
    if (!node)
        return std::nullopt;
    return std::optional<ExprPtr>(std::move(node));
}

} // namespace il::frontends::basic::constfold

namespace il::frontends::basic::detail
{
std::optional<Numeric> asNumeric(const Expr &e)
{
    if (auto *i = dynamic_cast<const IntExpr *>(&e))
        return Numeric{false, static_cast<double>(i->value), static_cast<long long>(i->value)};
    if (auto *f = dynamic_cast<const FloatExpr *>(&e))
        return Numeric{true, f->value, static_cast<long long>(f->value)};
    return std::nullopt;
}

Numeric promote(const Numeric &a, const Numeric &b)
{
    if (a.isFloat || b.isFloat)
        return Numeric{true, a.isFloat ? a.f : static_cast<double>(a.i), a.i};
    return a;
}

} // namespace il::frontends::basic::detail
