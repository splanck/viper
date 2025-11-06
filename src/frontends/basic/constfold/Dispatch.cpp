//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/constfold/Dispatch.cpp
// Purpose: Dispatch BASIC constant folding requests to domain-specific helpers
//          covering arithmetic, logical, comparison, cast, and string
//          operations.
// Key invariants: Dispatch never mutates the AST directly; it operates purely
//                 on @ref Constant summaries and only materialises fresh AST
//                 nodes when a fold succeeds.
// Ownership/Lifetime: Relies on caller-managed AST nodes and temporary
//                     constants; newly created AST nodes are returned via
//                     std::unique_ptr to transfer ownership back to the caller.
// Links: docs/basic-language.md, docs/codemap.md
//
// Dispatches BASIC constant folding requests to domain-specific helpers.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements the central dispatch logic for constant folding.

#include "frontends/basic/constfold/ConstantUtils.hpp"
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
/// @brief Determine whether a literal kind carries numeric semantics.
/// @param kind Literal classification to inspect.
/// @return True when the literal represents an integer or floating-point value.
bool is_numeric(LiteralKind kind)
{
    return kind == LiteralKind::Int || kind == LiteralKind::Float;
}

} // namespace

/// @brief Convert an AST expression into a numeric constant when possible.
/// @details Attempts to interpret integer and floating literal nodes as
///          @ref NumericValue, capturing both the floating and integer views so
///          downstream folding code can operate in whichever domain is
///          convenient.  Non-numeric expressions yield @c std::nullopt.
/// @param expr Expression candidate to inspect.
/// @return Numeric summary or empty optional when conversion fails.
std::optional<NumericValue> numeric_from_expr(const AST::Expr &expr)
{
    if (auto *i = dynamic_cast<const ::il::frontends::basic::IntExpr *>(&expr))
        return NumericValue{false, static_cast<double>(i->value), i->value};
    if (auto *f = dynamic_cast<const ::il::frontends::basic::FloatExpr *>(&expr))
        return NumericValue{true, f->value, static_cast<long long>(f->value)};
    return std::nullopt;
}

/// @brief Promote two numeric values to a compatible representation.
/// @details Returns a copy of @p lhs promoted to floating point when either
///          operand is a float; otherwise preserves the integer view.  The
///          helper centralises the promotion policy so callers do not have to
///          duplicate the decision logic.
/// @param lhs Left-hand numeric operand.
/// @param rhs Right-hand numeric operand.
/// @return Promoted numeric representation suitable for folding.
NumericValue promote_numeric(const NumericValue &lhs, const NumericValue &rhs)
{
    if (lhs.isFloat || rhs.isFloat)
        return NumericValue{true, lhs.isFloat ? lhs.f : static_cast<double>(lhs.i), lhs.i};
    return lhs;
}

namespace
{

/// @brief Summarise an AST literal into the internal @ref Constant form.
/// @details Handles integers, floats, booleans, and strings by populating the
///          appropriate fields on @ref Constant.  Non-literal expressions yield
///          @c std::nullopt so callers know folding cannot proceed.
/// @param expr Expression to inspect.
/// @return Populated constant or empty optional when @p expr is not a literal.
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
        return make_bool_constant(b->value);
    if (auto *s = dynamic_cast<const ::il::frontends::basic::StringExpr *>(&expr))
    {
        Constant c;
        c.kind = LiteralKind::String;
        c.stringValue = s->value;
        return c;
    }
    return std::nullopt;
}

/// @brief Construct a new AST literal node from a folded constant.
/// @details Allocates the appropriate AST node type for the constant's kind and
///          copies the stored value across.  The resulting unique_ptr transfers
///          ownership to the caller, who typically replaces an existing AST
///          subtree with the materialised literal.
/// @param constant Folded constant value to materialise.
/// @return Newly allocated AST expression representing @p constant.
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
            auto out = std::make_unique<AST::BoolExpr>();
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

/// @brief Infer which folding domain applies to a binary expression.
/// @details Examines the operator and operand literal kinds to decide whether
///          the expression should be handled by arithmetic, logical, comparison,
///          string, or cast folders.  Returns @c std::nullopt when the
///          combination cannot be folded at compile time (for example, mixing
///          string and numeric operands with subtraction).
/// @param op Binary operator under consideration.
/// @param lhs Literal kind of the left operand.
/// @param rhs Literal kind of the right operand.
/// @return Selected fold domain or empty optional when no fold applies.
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
/// @brief Compare two constants for equality across all literal kinds.
/// @details Used exclusively in debug builds to validate that folding helpers
///          produce stable results across alternative code paths.
/// @param lhs First constant to compare.
/// @param rhs Second constant to compare.
/// @return True when the constants represent identical values.
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

/// @brief Route a binary constant fold to the appropriate domain helper.
/// @details Switches on the deduced fold kind and calls the specialised folding
///          routine, forwarding the original operator and operands.  Returns the
///          folded constant on success or @c std::nullopt when the domain helper
///          cannot simplify the expression.
/// @param kind Folding domain selected by @ref deduce_kind.
/// @param op Binary operator being folded.
/// @param lhs Left-hand constant operand.
/// @param rhs Right-hand constant operand.
/// @return Folded constant or empty optional when folding is not possible.
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

/// @brief Determine whether a binary expression can be constant folded.
/// @details Verifies that the expression is a binary operation with literal
///          operands, deduces the folding domain, and performs a dry-run fold to
///          confirm the helper succeeds.  The actual AST is not mutated.
/// @param expr Candidate expression to analyse.
/// @return True when folding would succeed.
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

/// @brief Attempt to fold a binary expression into a constant AST node.
/// @details Mirrors @ref can_fold but, upon success, materialises a new literal
///          node that the caller can splice into the AST.  Debug builds include
///          sanity checks to ensure commutative folds are insensitive to operand
///          order.
/// @param expr Expression to fold.
/// @return New AST node containing the folded value or empty optional on failure.
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
