//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/utils/UseDefInfo.hpp
// Purpose: Provide use-def chain tracking for efficient SSA value replacement.
// Key invariants: Use lists are invalidated when instructions are added/removed.
// Ownership/Lifetime: UseDefInfo does not own the function; caller must ensure
//                     the function outlives the UseDefInfo instance.
// Links: docs/dev/analysis.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/core/Value.hpp"

#include <unordered_map>
#include <vector>

namespace il::core
{
struct Function;
struct BasicBlock;
struct Instr;
} // namespace il::core

namespace viper::il
{

/// @brief Tracks use locations for SSA temporaries to enable O(1) replacement.
/// @details Instead of scanning all instructions to replace uses of a temporary,
///          this class pre-computes the locations of all uses. Replacement then
///          only visits actual use sites, reducing O(n) scans to O(uses).
///
/// Usage:
/// @code
///   UseDefInfo info(F);  // Build use-def chains for function F
///   info.replaceAllUses(tempId, newValue);  // O(uses) instead of O(instructions)
/// @endcode
///
/// @warning The use-def info becomes stale if instructions are added, removed,
///          or have their operands modified through other means. Rebuild after
///          such modifications.
class UseDefInfo
{
  public:
    /// @brief Construct use-def chains for all temporaries in function @p F.
    /// @param F Function to analyze.
    explicit UseDefInfo(::il::core::Function &F);

    /// @brief Replace all uses of temporary @p tempId with @p replacement.
    /// @details Only visits actual use sites, providing O(uses) complexity
    ///          instead of O(instructions) for full function scans.
    /// @param tempId Temporary identifier to replace.
    /// @param replacement New value to substitute.
    /// @return Number of uses replaced.
    std::size_t replaceAllUses(unsigned tempId, const ::il::core::Value &replacement);

    /// @brief Check if a temporary has any uses.
    /// @param tempId Temporary identifier to check.
    /// @return True if the temporary has at least one use.
    [[nodiscard]] bool hasUses(unsigned tempId) const;

    /// @brief Count the number of uses of a temporary.
    /// @param tempId Temporary identifier to count.
    /// @return Number of uses (0 if not found).
    [[nodiscard]] std::size_t useCount(unsigned tempId) const;

  private:
    /// @brief Pointer to a Value that can be updated in place.
    using UsePtr = ::il::core::Value *;

    /// @brief Map from temporary ID to list of use pointers.
    std::unordered_map<unsigned, std::vector<UsePtr>> uses_;

    /// @brief Scan a function and populate the use map.
    void build(::il::core::Function &F);

    /// @brief Record a use if the value is a temporary.
    void recordUse(::il::core::Value &v);
};

} // namespace viper::il
