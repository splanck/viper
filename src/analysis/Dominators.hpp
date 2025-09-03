// File: src/analysis/Dominators.hpp
// Purpose: Declares dominator tree construction for functions.
// Key invariants: Requires valid CFG postorder numbering.
// Ownership/Lifetime: Holds non-owning pointers to blocks.
// Links: docs/dev/analysis.md
#pragma once

#include "analysis/CFG.hpp"
#include <unordered_map>

namespace il::analysis
{

class DominatorTree
{
  public:
    explicit DominatorTree(const CFG &cfg);

    const il::core::BasicBlock *idom(const il::core::BasicBlock *b) const;
    bool dominates(const il::core::BasicBlock *a, const il::core::BasicBlock *b) const;

  private:
    using Block = il::core::BasicBlock;
    const CFG &cfg;
    std::unordered_map<const Block *, const Block *> idoms;

    const Block *intersect(const Block *a, const Block *b) const;
};

} // namespace il::analysis
