//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the dominator tree analysis for IL functions. The dominator
// tree captures dominance relationships in the control flow graph: block A dominates
// block B if every path from the function entry to B must pass through A. This
// fundamental analysis enables many optimizations including SSA construction,
// loop analysis, and code motion.
//
// Dominator Tree Properties:
// - A block dominates itself
// - The entry block dominates all reachable blocks
// - The immediate dominator (idom) of a block B is the unique dominator that is
//   closest to B in the CFG
// - The dominator tree has the entry block as root, with edges from each block
//   to its immediate dominator
//
// This analysis uses the Lengauer-Tarjan algorithm for efficient dominator tree
// construction with near-linear time complexity. The tree is computed eagerly when
// analysis is requested and cached until invalidated by transformations.
//
// Key Queries:
// - dominates(A, B): Returns true if A dominates B
// - getImmediateDominator(B): Returns the immediate dominator of B
// - dominanceFrontier(B): Computes blocks where B's dominance ends
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
