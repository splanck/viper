//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/analysis/Dominators.hpp
// Purpose: Dominator tree analysis for IL functions. Captures dominance
//          relationships (block A dominates block B iff every path from
//          entry to B passes through A). Provides dominates() and
//          immediateDominator() queries, built via Lengauer-Tarjan.
// Key invariants:
//   - A block dominates itself; the entry block dominates all reachable blocks.
//   - immediateDominator() returns nullptr only for the entry block.
//   - DomTree must be recomputed after any CFG mutation.
// Ownership/Lifetime: DomTree owns its idom and children maps by value.
//          Computed from a CFGContext + Function; block pointers must remain
//          stable while the DomTree is in use.
// Links: il/analysis/CFG.hpp, il/core/Function.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <unordered_map>
#include <vector>

namespace il::core
{
struct Function;
struct BasicBlock;
using Block = BasicBlock;
} // namespace il::core

namespace viper::analysis
{
struct CFGContext;

/// @brief Dominator tree for a function.
/// Stores immediate dominator relationships and tree children for each block.
struct DomTree
{
    std::unordered_map<il::core::Block *, il::core::Block *>
        idom; ///< Maps each block to its immediate dominator
    std::unordered_map<il::core::Block *, std::vector<il::core::Block *>>
        children; ///< Maps each block to the blocks it immediately dominates

    /// @brief Return true if block @p A dominates block @p B.
    /// @param A Potential dominator.
    /// @param B Block being checked.
    /// @return True if A dominates B.
    bool dominates(il::core::Block *A, il::core::Block *B) const;

    /// @brief Return the immediate dominator of block @p B.
    /// @param B Block whose immediate dominator is requested.
    /// @return Immediate dominator or nullptr for the entry block.
    il::core::Block *immediateDominator(il::core::Block *B) const;
};

/// @brief Compute dominator tree for function @p F.
/// @param ctx CFG context providing traversal utilities.
/// @param F Function to analyze.
/// @return Dominator tree with immediate dominators and child map.
DomTree computeDominatorTree(const CFGContext &ctx, il::core::Function &F);

} // namespace viper::analysis
