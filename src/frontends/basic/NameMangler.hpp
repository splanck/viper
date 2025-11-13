//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the NameMangler class, which generates deterministic,
// unique names for IL symbols, temporaries, and basic blocks during the
// lowering process.
//
// Name mangling is essential for translating BASIC's human-readable identifiers
// into IL's internal representation while ensuring:
// - Uniqueness: No name collisions between user variables, temporaries, blocks
// - Determinism: Identical BASIC programs always produce identical IL names
// - Readability: Generated names remain somewhat human-readable for debugging
//
// Key Responsibilities:
// - Temporary naming: Generates unique temporary names for intermediate values
//   in expression lowering (e.g., %t0, %t1, %t2)
// - Block naming: Creates unique basic block labels based on semantic hints
//   (e.g., "entry", "then", "else", "loop_body")
// - Collision handling: Appends numeric suffixes when a block name hint is
//   reused (e.g., "then", "then_1", "then_2")
//
// Naming Conventions:
// - Temporaries: %t<N> where N is a monotonically increasing counter
//   * Example: %t0, %t1, %t2, ...
// - Blocks: <hint> or <hint>_<N> for subsequent uses
//   * Example: "entry", "loop_body", "then", "then_1", "else"
//
// Usage Pattern:
// During lowering, the NameMangler is used to generate fresh names:
//   auto tempName = mangler.nextTemp();        // "%t5"
//   auto blockName = mangler.block("then");    // "then"
//   auto blockName2 = mangler.block("then");   // "then_1"
//
// Integration:
// - Used by: Lowerer and all lowering helper classes (LowerExpr*, LowerStmt*)
// - Ensures: Deterministic IL output for reproducible builds and testing
// - Scoped to: Each compilation unit (new NameMangler per BASIC program)
//
// Design Notes:
// - Stateful class; maintains counters for deterministic name generation
// - Not thread-safe; each compilation uses its own instance
// - Simple counter-based approach ensures O(1) name generation
// - Block name hints improve IL readability without affecting correctness
//
//===----------------------------------------------------------------------===//
#pragma once

#include <string>
#include <unordered_map>

namespace il::frontends::basic
{

/// @brief Generates deterministic names for temporaries and blocks.
/// @invariant Temp IDs increase sequentially; block names gain numeric suffixes on collision.
/// @ownership Pure utility; no external ownership.
class NameMangler
{
  public:
    /// @brief Return next temporary name ("%t0", "%t1", ...).
    std::string nextTemp();

    /// @brief Return a block label based on @p hint ("entry", "then", ...).
    /// If the hint was used before, a numeric suffix is appended.
    std::string block(const std::string &hint);

  private:
    /// @brief Monotonically increasing ID for temporary names.
    unsigned tempCounter{0};
    /// @brief Map of block name hints to the number of times they've been used.
    std::unordered_map<std::string, unsigned> blockCounters;
};

} // namespace il::frontends::basic
