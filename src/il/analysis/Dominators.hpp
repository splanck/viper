// File: src/il/analysis/Dominators.hpp
// Purpose: Declares a simple dominator tree builder.
// Key invariants: Tree is rooted at function entry and deterministic.
// Ownership/Lifetime: Holds non-owning pointers to function blocks.
// Links: docs/dev/analysis.md
#pragma once

#include "il/analysis/CFG.hpp"
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::analysis
{

class DominatorTree
{
  public:
    explicit DominatorTree(const CFG &cfg);

    const il::core::BasicBlock *idom(const il::core::BasicBlock &bb) const;

    bool dominates(const il::core::BasicBlock &a, const il::core::BasicBlock &b) const;

  private:
    std::vector<const il::core::BasicBlock *> blocks_;
    std::unordered_map<const il::core::BasicBlock *,
                       std::unordered_set<const il::core::BasicBlock *>>
        doms_;
    std::unordered_map<const il::core::BasicBlock *, std::size_t> index_;
    std::unordered_map<const il::core::BasicBlock *, const il::core::BasicBlock *> idom_;
};

} // namespace il::analysis
