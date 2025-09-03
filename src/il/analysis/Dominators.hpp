// File: src/il/analysis/Dominators.hpp
// Purpose: Compute dominator relationships for basic blocks.
// Key invariants: Entry block dominates all others.
// Ownership/Lifetime: Relies on CFG data; does not own blocks.
// Links: docs/class-catalog.md
#pragma once

#include "il/analysis/CFG.hpp"
#include <unordered_map>

namespace il::analysis
{

/// @brief Simple dominator tree using Cooper et al.'s algorithm.
class DominatorTree
{
  public:
    /// @brief Build dominator tree for @p cfg.
    explicit DominatorTree(const CFG &cfg);

    /// @brief Immediate dominator of @p b; nullptr for entry.
    const core::BasicBlock *idom(const core::BasicBlock &b) const;

    /// @brief Whether @p a dominates @p b.
    bool dominates(const core::BasicBlock &a, const core::BasicBlock &b) const;

  private:
    const CFG &cfg_;
    std::unordered_map<const core::BasicBlock *, const core::BasicBlock *> idom_;
    std::unordered_map<const core::BasicBlock *, unsigned> rpoIndex_;

    const core::BasicBlock *intersect(const core::BasicBlock *a, const core::BasicBlock *b) const;
};

} // namespace il::analysis
