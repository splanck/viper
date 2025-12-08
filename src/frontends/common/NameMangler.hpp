//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: frontends/common/NameMangler.hpp
// Purpose: Generates deterministic, unique names for IL symbols during lowering.
//
// This header provides a NameMangler class that generates unique names for
// temporaries and basic blocks during AST-to-IL lowering. It is shared across
// multiple language frontends (BASIC, Pascal, etc.).
//
// Name mangling is essential for translating source language identifiers
// into IL's internal representation while ensuring:
// - Uniqueness: No name collisions between user variables, temporaries, blocks
// - Determinism: Identical programs always produce identical IL names
// - Readability: Generated names remain somewhat human-readable for debugging
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <unordered_map>

namespace il::frontends::common
{

/// @brief Generates deterministic names for temporaries and blocks.
/// @details Used during AST-to-IL lowering to create unique names.
/// @invariant Temp IDs increase sequentially; block names gain numeric suffixes on collision.
/// @ownership Pure utility; no external ownership.
class NameMangler
{
  public:
    /// @brief Construct a NameMangler with default temp prefix "%t".
    NameMangler() = default;

    /// @brief Construct a NameMangler with a custom temp prefix.
    /// @param tempPrefix Prefix for temporary names (e.g., "%t", "$tmp")
    explicit NameMangler(std::string tempPrefix) : tempPrefix_(std::move(tempPrefix)) {}

    /// @brief Return next temporary name (e.g., "%t0", "%t1", ...).
    /// @return A unique temporary name using the configured prefix.
    std::string nextTemp()
    {
        return tempPrefix_ + std::to_string(tempCounter_++);
    }

    /// @brief Return a block label based on @p hint ("entry", "then", ...).
    /// @details If the hint was used before, a numeric suffix is appended.
    /// @param hint The semantic hint for the block name.
    /// @return A unique block name, possibly with a numeric suffix.
    std::string block(const std::string &hint)
    {
        auto &count = blockCounters_[hint];
        std::string name = hint;
        if (count > 0)
            name += std::to_string(count);
        ++count;
        return name;
    }

    /// @brief Reset all counters for a new compilation unit.
    void reset()
    {
        tempCounter_ = 0;
        blockCounters_.clear();
    }

    /// @brief Get the current temp counter value (for debugging/testing).
    unsigned tempCount() const { return tempCounter_; }

  private:
    /// @brief Prefix for temporary names (default: "%t").
    std::string tempPrefix_ = "%t";

    /// @brief Monotonically increasing ID for temporary names.
    unsigned tempCounter_ = 0;

    /// @brief Map of block name hints to the number of times they've been used.
    std::unordered_map<std::string, unsigned> blockCounters_;
};

} // namespace il::frontends::common
