// File: src/analysis/Dominators.hpp
// Purpose: Construct dominator tree for a function.
// Key invariants: Deterministic Cooper-style algorithm.
// Ownership/Lifetime: References blocks owned elsewhere.
// Links: docs/dev/analysis.md
#pragma once

#include "analysis/CFG.hpp"
#include <unordered_map>

namespace il::analysis
{

/// @brief Computes immediate dominators and dominance queries.
class DominatorTree
{
  public:
    /// @brief Build dominator tree from CFG @p cfg.
    explicit DominatorTree(const CFG &cfg);

    /// @brief Immediate dominator of block @p bb (nullptr for entry).
    const il::core::BasicBlock *idom(const il::core::BasicBlock &bb) const;

    /// @brief True if @p a dominates @p b.
    bool dominates(const il::core::BasicBlock &a, const il::core::BasicBlock &b) const;

  private:
    std::unordered_map<const il::core::BasicBlock *, const il::core::BasicBlock *> idoms;
};

} // namespace il::analysis
