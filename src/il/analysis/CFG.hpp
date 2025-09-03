// File: src/il/analysis/CFG.hpp
// Purpose: Builds a control-flow graph for IL functions.
// Key invariants: Successor/predecessor lists mirror terminator labels.
// Ownership/Lifetime: Non-owning pointers into the function's blocks.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/Function.hpp"
#include <unordered_map>
#include <vector>

namespace il::analysis
{

class CFG
{
  public:
    explicit CFG(const il::core::Function &f);

    const std::vector<const il::core::BasicBlock *> &successors(
        const il::core::BasicBlock &bb) const;

    const std::vector<const il::core::BasicBlock *> &predecessors(
        const il::core::BasicBlock &bb) const;

    const std::vector<const il::core::BasicBlock *> &postOrder() const;

  private:
    std::unordered_map<const il::core::BasicBlock *, std::vector<const il::core::BasicBlock *>>
        succ_;
    std::unordered_map<const il::core::BasicBlock *, std::vector<const il::core::BasicBlock *>>
        pred_;
    std::vector<const il::core::BasicBlock *> post_;
};

} // namespace il::analysis
