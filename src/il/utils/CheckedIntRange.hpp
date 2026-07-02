//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/utils/CheckedIntRange.hpp
// Purpose: Shared checked signed-integer arithmetic and range-combination
//          helpers for verifier and optimization proofs.
// Key invariants: Helpers never perform signed overflow; operations that cannot
//                 be represented return std::nullopt.
// Ownership/Lifetime: Value-only helpers with no dynamic ownership.
// Links: il/verify/InstructionChecker.cpp, il/transform/CheckOpt.cpp
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Shared checked integer range arithmetic for IL proof code.
/// @details The verifier and CheckOpt both reason about whether plain signed
///          arithmetic is justified by constants or incoming range facts. This
///          header centralizes the overflow checks and range combination rules
///          so both users apply identical arithmetic semantics.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace il::utils {

/// @brief Inclusive signed 64-bit integer range with optional bounds.
/// @details A missing lower or upper bound represents an unbounded side of the
///          range. Operations only return a concrete range when every required
///          endpoint computation can be performed without signed overflow.
struct IntRange {
    std::optional<int64_t> lower; ///< Inclusive lower bound when known.
    std::optional<int64_t> upper; ///< Inclusive upper bound when known.
};

/// @brief Check whether signed addition would overflow int64_t.
/// @param lhs Left addend.
/// @param rhs Right addend.
/// @return True when `lhs + rhs` cannot be represented as int64_t.
inline bool addOverflows(int64_t lhs, int64_t rhs) {
    if (rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs)
        return true;
    if (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)
        return true;
    return false;
}

/// @brief Check whether signed subtraction would overflow int64_t.
/// @param lhs Minuend.
/// @param rhs Subtrahend.
/// @return True when `lhs - rhs` cannot be represented as int64_t.
inline bool subOverflows(int64_t lhs, int64_t rhs) {
    if (rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs)
        return true;
    if (rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs)
        return true;
    return false;
}

/// @brief Check whether signed multiplication would overflow int64_t.
/// @param lhs Left factor.
/// @param rhs Right factor.
/// @return True when `lhs * rhs` cannot be represented as int64_t.
inline bool mulOverflows(int64_t lhs, int64_t rhs) {
    if (lhs == 0 || rhs == 0)
        return false;
    if (lhs == -1)
        return rhs == std::numeric_limits<int64_t>::min();
    if (rhs == -1)
        return lhs == std::numeric_limits<int64_t>::min();
    if ((lhs > 0) == (rhs > 0))
        return lhs > std::numeric_limits<int64_t>::max() / rhs;
    return lhs < std::numeric_limits<int64_t>::min() / rhs;
}

/// @brief Create an exact one-value range.
/// @param value Signed integer value represented by the range.
/// @return Range whose lower and upper bounds are both @p value.
inline IntRange exactRange(int64_t value) {
    return IntRange{value, value};
}

/// @brief Add two signed values if the result is representable.
/// @param lhs Left addend.
/// @param rhs Right addend.
/// @return Sum when no overflow occurs; otherwise std::nullopt.
inline std::optional<int64_t> addCheckedValue(int64_t lhs, int64_t rhs) {
    if (addOverflows(lhs, rhs))
        return std::nullopt;
    return lhs + rhs;
}

/// @brief Subtract two signed values if the result is representable.
/// @param lhs Minuend.
/// @param rhs Subtrahend.
/// @return Difference when no overflow occurs; otherwise std::nullopt.
inline std::optional<int64_t> subCheckedValue(int64_t lhs, int64_t rhs) {
    if (subOverflows(lhs, rhs))
        return std::nullopt;
    return lhs - rhs;
}

/// @brief Multiply two signed values if the result is representable.
/// @param lhs Left factor.
/// @param rhs Right factor.
/// @return Product when no overflow occurs; otherwise std::nullopt.
inline std::optional<int64_t> mulCheckedValue(int64_t lhs, int64_t rhs) {
    if (mulOverflows(lhs, rhs))
        return std::nullopt;
    return lhs * rhs;
}

/// @brief Combine two ranges with signed addition.
/// @details Missing bounds are treated as the corresponding int64_t extreme
///          only for overflow checking. A result bound is emitted only when at
///          least one input contributed a concrete bound for that side.
/// @param lhs Left operand range.
/// @param rhs Right operand range.
/// @return Sum range, or std::nullopt when an endpoint overflows.
inline std::optional<IntRange> addRanges(const IntRange &lhs, const IntRange &rhs) {
    const int64_t lhsLower = lhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t lhsUpper = lhs.upper.value_or(std::numeric_limits<int64_t>::max());
    const int64_t rhsLower = rhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t rhsUpper = rhs.upper.value_or(std::numeric_limits<int64_t>::max());
    auto lower = addCheckedValue(lhsLower, rhsLower);
    auto upper = addCheckedValue(lhsUpper, rhsUpper);
    if (!lower || !upper)
        return std::nullopt;

    IntRange result;
    if (lhs.lower || rhs.lower)
        result.lower = *lower;
    if (lhs.upper || rhs.upper)
        result.upper = *upper;
    return result;
}

/// @brief Combine two ranges with signed subtraction.
/// @details Subtraction uses the right upper endpoint for the lower bound and
///          the right lower endpoint for the upper bound. Bounds are omitted
///          when neither input side provides enough information.
/// @param lhs Left operand range.
/// @param rhs Right operand range.
/// @return Difference range, or std::nullopt when an endpoint overflows.
inline std::optional<IntRange> subRanges(const IntRange &lhs, const IntRange &rhs) {
    const int64_t lhsLower = lhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t lhsUpper = lhs.upper.value_or(std::numeric_limits<int64_t>::max());
    const int64_t rhsLower = rhs.lower.value_or(std::numeric_limits<int64_t>::min());
    const int64_t rhsUpper = rhs.upper.value_or(std::numeric_limits<int64_t>::max());
    auto lower = subCheckedValue(lhsLower, rhsUpper);
    auto upper = subCheckedValue(lhsUpper, rhsLower);
    if (!lower || !upper)
        return std::nullopt;

    IntRange result;
    if (lhs.lower || rhs.upper)
        result.lower = *lower;
    if (lhs.upper || rhs.lower)
        result.upper = *upper;
    return result;
}

/// @brief Combine two fully bounded ranges with signed multiplication.
/// @details The product bounds are derived from the four endpoint products. If
///          any endpoint product would overflow, the result is unknown.
/// @param lhs Left operand range. Both bounds must be present.
/// @param rhs Right operand range. Both bounds must be present.
/// @return Product range, or std::nullopt when bounds are missing or overflow.
inline std::optional<IntRange> mulRanges(const IntRange &lhs, const IntRange &rhs) {
    if (!lhs.lower || !lhs.upper || !rhs.lower || !rhs.upper)
        return std::nullopt;

    std::array<std::optional<int64_t>, 4> products{
        mulCheckedValue(*lhs.lower, *rhs.lower),
        mulCheckedValue(*lhs.lower, *rhs.upper),
        mulCheckedValue(*lhs.upper, *rhs.lower),
        mulCheckedValue(*lhs.upper, *rhs.upper),
    };
    for (const auto &product : products)
        if (!product)
            return std::nullopt;

    int64_t lo = *products[0];
    int64_t hi = *products[0];
    for (const auto &product : products) {
        lo = std::min(lo, *product);
        hi = std::max(hi, *product);
    }
    return IntRange{lo, hi};
}

/// @brief Merge two incoming range facts at a control-flow join.
/// @details The merged lower bound is the minimum of known lower bounds, and
///          the merged upper bound is the maximum of known upper bounds. If no
///          side has matching concrete bounds, the fact is discarded.
/// @param lhs First incoming range fact.
/// @param rhs Second incoming range fact.
/// @return Merged fact, or std::nullopt when the facts share no known bounds.
inline std::optional<IntRange> mergeIncomingRange(const IntRange &lhs, const IntRange &rhs) {
    IntRange merged;
    if (lhs.lower && rhs.lower)
        merged.lower = std::min(*lhs.lower, *rhs.lower);
    if (lhs.upper && rhs.upper)
        merged.upper = std::max(*lhs.upper, *rhs.upper);
    if (!merged.lower && !merged.upper)
        return std::nullopt;
    return merged;
}

/// @brief Intersect two range facts that are simultaneously known for a value.
/// @details The intersection keeps the tighter bound on each side. Used when a
///          flow fact meets a branch-condition fact on the same CFG edge. An
///          empty intersection (lower > upper) means the edge is infeasible;
///          the caller may still use the result soundly because no execution
///          reaches the target with that value.
/// @param lhs First known range fact.
/// @param rhs Second known range fact.
/// @return Intersected fact, or std::nullopt when neither side is bounded.
inline std::optional<IntRange> intersectRanges(const IntRange &lhs, const IntRange &rhs) {
    IntRange out;
    if (lhs.lower && rhs.lower)
        out.lower = std::max(*lhs.lower, *rhs.lower);
    else if (lhs.lower)
        out.lower = lhs.lower;
    else if (rhs.lower)
        out.lower = rhs.lower;
    if (lhs.upper && rhs.upper)
        out.upper = std::min(*lhs.upper, *rhs.upper);
    else if (lhs.upper)
        out.upper = lhs.upper;
    else if (rhs.upper)
        out.upper = rhs.upper;
    if (!out.lower && !out.upper)
        return std::nullopt;
    return out;
}

} // namespace il::utils
