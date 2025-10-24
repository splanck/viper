// File: src/il/analysis/Dominators.hpp
// Purpose: Dominator tree computation and queries.
// Key invariants: Dominator tree is computed eagerly and remains constant; no incremental updates.
// Ownership/Lifetime: Operates on IL blocks owned by the caller.
// Links: docs/dev/analysis.md
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
