// File: src/il/analysis/LoopInfo.hpp
// Purpose: Describe natural loop structures discovered in IL control-flow graphs.
// Key invariants: Loop headers dominate their bodies; block lists are unique and header-inclusive.
// Ownership/Lifetime: Views over IL basic blocks owned by the parent module/function.
// Links: docs/dev/analysis.md
#pragma once

#include <memory>
#include <vector>

namespace il::core
{
struct BasicBlock;
struct Function;
struct Module;
} // namespace il::core

namespace viper::analysis
{

class DomTree;

/// @brief Represents a single natural loop discovered in a function.
/// @details Captures the header, latches, contained blocks, exits, and the nesting
/// structure between loops. The lifetime of the referenced basic blocks is owned by
/// the parent function; Loop merely provides non-owning views to support analysis.
struct Loop
{
    il::core::BasicBlock *Header = nullptr;                     ///< Loop header block.
    std::vector<il::core::BasicBlock *> Blocks;                 ///< Blocks participating in the loop.
    std::vector<il::core::BasicBlock *> Latches;                ///< Blocks with backedges to the header.
    std::vector<il::core::BasicBlock *> Exits;                  ///< Successors that leave the loop.
    Loop *Parent = nullptr;                                     ///< Immediate parent loop or nullptr.
    std::vector<std::unique_ptr<Loop>> Children;                ///< Nested child loops.
};

/// @brief Aggregates loop information for a function.
/// @details Provides access to the forest of natural loops rooted at top-level headers
/// and queries for locating the innermost loop containing a basic block. Instances are
/// produced by analysing a function using dominator and CFG information.
class LoopInfo
{
  public:
    /// @brief Analyse @p function to discover natural loops.
    /// @param module Module containing the function under analysis.
    /// @param function Function whose loops are requested.
    /// @param dom Dominator tree previously computed for @p function.
    /// @return Populated loop forest describing all natural loops in the function.
    static LoopInfo compute(il::core::Module &module, il::core::Function &function, const DomTree &dom);

    /// @brief Retrieve the innermost loop containing @p block.
    /// @param block Block to query.
    /// @return Pointer to the loop containing @p block or nullptr when none.
    const Loop *getLoopFor(const il::core::BasicBlock *block) const noexcept;

    /// @brief Access top-level loops in the function.
    /// @return Vector of unique pointers to top-level loops.
    const std::vector<std::unique_ptr<Loop>> &topLevelLoops() const noexcept
    {
        return TopLevel;
    }

  private:
    std::vector<std::unique_ptr<Loop>> TopLevel;
};

} // namespace viper::analysis
