//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// This source file is part of the Viper project.
//
// File: src/frontends/basic/constfold/FoldCompare.cpp
// Purpose: Implement the comparison-specific branch of the BASIC constant
//          folder so equality and ordering operations can be reduced at parse
//          time when both operands are literal expressions.
// Key invariants: Maintains IEEE semantics for floating-point comparisons and
//                 honours BASIC's three-way comparison rules, including
//                 propagation of unordered results for NaNs.
// Ownership/Lifetime: Operates entirely on lightweight Value helpers without
//                     owning AST nodes; callers remain responsible for
//                     replacing nodes within the tree.
// Links: docs/codemap.md, docs/il-guide.md#basic-frontend-constant-folding
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements comparison folding utilities.
/// @details The helpers translate AST comparison operators into pre-computed
///          truth tables so constant folding produces the same results that the
///          runtime would. Floating-point operands mirror IEEE ordering rules,
///          including propagation of unordered outcomes when NaNs appear in the
///          input.

#include "frontends/basic/constfold/ConstantUtils.hpp"
#include "frontends/basic/constfold/Dispatch.hpp"
#include "frontends/basic/constfold/Value.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace il::frontends::basic::constfold
{
namespace
{
enum class Outcome : std::size_t
{
    Less = 0,
    Equal = 1,
    Greater = 2,
    Unordered = 3,
};

constexpr std::size_t kOpCount = static_cast<std::size_t>(AST::BinaryExpr::Op::LogicalOr) + 1;

using BinOpFn = std::optional<bool> (*)(Value, Value);

struct TruthRow
{
    AST::BinaryExpr::Op op;
    std::array<std::optional<bool>, 4> truth;
};

constexpr std::array<TruthRow, 6> kTruthTable = {
    {{AST::BinaryExpr::Op::Eq, {false, true, false, false}},
     {AST::BinaryExpr::Op::Ne, {true, false, true, true}},
     {AST::BinaryExpr::Op::Lt, {true, false, false, std::nullopt}},
     {AST::BinaryExpr::Op::Le, {true, true, false, std::nullopt}},
     {AST::BinaryExpr::Op::Gt, {false, false, true, std::nullopt}},
     {AST::BinaryExpr::Op::Ge, {false, true, true, std::nullopt}}}};

/// @brief Map a comparison outcome to the folded literal for @p op.
/// @details Looks up the requested operator in @ref kTruthTable and returns the
///          matching literal when available. Operators that do not define a
///          result for the supplied @p outcome, such as unordered `<`, yield
///          @ref Value::invalid so the dispatcher can decline the fold.
/// @param op BASIC AST opcode describing the comparison to evaluate.
/// @param outcome Precomputed comparison category for the literal operands.
/// @return Folded literal or @ref Value::invalid when the combination is
///         unsupported.
[[nodiscard]] std::optional<bool> from_truth(AST::BinaryExpr::Op op, Outcome outcome)
{
    for (const auto &row : kTruthTable)
    {
        if (row.op == op)
        {
            return row.truth[static_cast<std::size_t>(outcome)];
        }
    }
    return std::nullopt;
}

/// @brief Compute the three-way ordering between two literal operands.
/// @details Promotes to floating point when necessary so BASIC's numeric
///          widening rules are honoured. Propagates @ref Outcome::Unordered when
///          either side is NaN, preserving IEEE comparison semantics.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Comparison outcome describing the relationship between operands.
[[nodiscard]] Outcome compare_ordered(Value lhs, Value rhs)
{
    if (lhs.isFloat() || rhs.isFloat())
    {
        double lv = lhs.asDouble();
        double rv = rhs.asDouble();
        if (std::isnan(lv) || std::isnan(rv))
            return Outcome::Unordered;
        if (lv < rv)
            return Outcome::Less;
        if (lv > rv)
            return Outcome::Greater;
        return Outcome::Equal;
    }
    if (lhs.i < rhs.i)
        return Outcome::Less;
    if (lhs.i > rhs.i)
        return Outcome::Greater;
    return Outcome::Equal;
}

/// @brief Fold the equality operator for literal operands.
/// @details Delegates to @ref compare_ordered and maps the result through
///          @ref from_truth to produce a boolean literal. NaN involvement yields
///          @ref Value::invalid so the caller can skip folding.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded literal representing the equality result.
std::optional<bool> fold_eq(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Eq, compare_ordered(lhs, rhs));
}

/// @brief Fold the not-equal operator for literal operands.
/// @details Translates the operands via @ref compare_ordered and returns the
///          corresponding truth-table value. Unordered comparisons collapse to
///          `true`, matching runtime behaviour when NaNs are present.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded literal representing the not-equal result.
std::optional<bool> fold_ne(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Ne, compare_ordered(lhs, rhs));
}

/// @brief Fold the less-than operator for literal operands.
/// @details Produces a boolean literal when the comparison is ordered and
///          yields @ref Value::invalid when NaNs prevent establishing an order.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded literal or @ref Value::invalid when folding is unsafe.
std::optional<bool> fold_lt(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Lt, compare_ordered(lhs, rhs));
}

/// @brief Fold the less-or-equal operator for literal operands.
/// @details Builds on @ref compare_ordered to support equality in addition to
///          strict ordering. NaNs again prevent folding.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded literal for the `<=` predicate or @ref Value::invalid.
std::optional<bool> fold_le(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Le, compare_ordered(lhs, rhs));
}

/// @brief Fold the greater-than operator for literal operands.
/// @details Shares logic with @ref fold_lt while reflecting the swapped operand
///          semantics encoded in @ref kTruthTable.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded literal for the `>` predicate or invalid when unordered.
std::optional<bool> fold_gt(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Gt, compare_ordered(lhs, rhs));
}

/// @brief Fold the greater-or-equal operator for literal operands.
/// @details Mirrors @ref fold_le for the reversed comparison, including NaN
///          handling and boolean literal emission.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded literal representing the `>=` predicate or invalid.
std::optional<bool> fold_ge(Value lhs, Value rhs)
{
    return from_truth(AST::BinaryExpr::Op::Ge, compare_ordered(lhs, rhs));
}

/// @brief Build the dispatch table associating comparison ops with folders.
/// @details Initialises a dense array sized for all binary operators, installs
///          folding callbacks for the comparison subset, and leaves other
///          entries null so the dispatcher can efficiently skip unsupported
///          operations.
/// @return Table mapping @ref AST::BinaryExpr::Op indices to folding routines.
constexpr std::array<BinOpFn, kOpCount> make_compare_table()
{
    std::array<BinOpFn, kOpCount> table{};
    table.fill(nullptr);
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Eq)] = &fold_eq;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Ne)] = &fold_ne;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Lt)] = &fold_lt;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Le)] = &fold_le;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Gt)] = &fold_gt;
    table[static_cast<std::size_t>(AST::BinaryExpr::Op::Ge)] = &fold_ge;
    return table;
}

constexpr auto kCompareFold = make_compare_table();

/// @brief Convert a BASIC constant into the internal Value representation.
/// @details Handles both parsed numeric payloads and cases where only the
///          literal spelling is available. Attempting to parse invalid literals
///          mirrors runtime coercions so folding results stay consistent.
/// @param constant AST constant extracted from a literal expression.
/// @return Value describing the literal, or @c std::nullopt when conversion
///         fails.
[[nodiscard]] std::optional<Value> makeValueFromConstant(const Constant &constant)
{
    if (constant.kind == LiteralKind::Int || constant.kind == LiteralKind::Float)
    {
        if (constant.stringValue.empty())
            return makeValue(constant.numeric);

        auto parsed = detail::parseNumericLiteral(constant.stringValue);
        if (parsed.ok)
            return parsed.isFloat ? Value::fromFloat(parsed.d) : Value::fromInt(parsed.i);
        return makeValue(constant.numeric);
    }

    if (constant.kind == LiteralKind::Invalid && !constant.stringValue.empty())
    {
        auto parsed = detail::parseNumericLiteral(constant.stringValue);
        if (!parsed.ok)
            return std::nullopt;
        return parsed.isFloat ? Value::fromFloat(parsed.d) : Value::fromInt(parsed.i);
    }

    return std::nullopt;
}

/// @brief Attempt to fold a comparison using the prepared dispatch table.
/// @details Validates both operands before indexing into @ref kCompareFold. When
///          a folding routine is available the operands are promoted to a common
///          type, the folder invoked, and its result returned. Invalid results
///          signal that folding should be abandoned.
/// @param op Comparison opcode being folded.
/// @param lhs Left-hand literal operand.
/// @param rhs Right-hand literal operand.
/// @return Folded value or @c std::nullopt when folding is not possible.
std::optional<bool> tryFold(AST::BinaryExpr::Op op, Value lhs, Value rhs)
{
    if (!lhs.valid || !rhs.valid)
        return std::nullopt;
    const auto index = static_cast<std::size_t>(op);
    if (index >= kCompareFold.size())
        return std::nullopt;
    auto fn = kCompareFold[index];
    if (!fn)
        return std::nullopt;
    auto promoted = promote(lhs, rhs);
    return fn(promoted.first, promoted.second);
}

} // namespace

/// @brief Fold comparison expressions when both operands are literal constants.
/// @details Handles string equality directly because the Value helper focuses on
///          numeric operands. For numeric comparisons the function converts to
///          @ref Value wrappers, dispatches to @ref tryFold, and converts the
///          result back into a @ref Constant when successful.
/// @param op Comparison opcode describing the expression.
/// @param lhs Left-hand literal constant.
/// @param rhs Right-hand literal constant.
/// @return Folded constant when the comparison can be evaluated eagerly.
std::optional<Constant> fold_compare(AST::BinaryExpr::Op op,
                                     const Constant &lhs,
                                     const Constant &rhs)
{
    if (lhs.kind == LiteralKind::String && rhs.kind == LiteralKind::String)
    {
        if (op != AST::BinaryExpr::Op::Eq && op != AST::BinaryExpr::Op::Ne)
            return std::nullopt;
        const bool eq = lhs.stringValue == rhs.stringValue;
        const bool result = (op == AST::BinaryExpr::Op::Eq) ? eq : !eq;
        return make_bool_constant(result);
    }

    auto lhsValue = makeValueFromConstant(lhs);
    auto rhsValue = makeValueFromConstant(rhs);
    if (!lhsValue || !rhsValue)
        return std::nullopt;

    auto folded = tryFold(op, *lhsValue, *rhsValue);
    if (!folded)
        return std::nullopt;

    return make_bool_constant(*folded);
}

} // namespace il::frontends::basic::constfold
