// File: src/analysis/CFG.hpp
// Purpose: Builds predecessor/successor lists and postorder for functions.
// Key invariants: Blocks referenced by labels must exist in the function.
// Ownership/Lifetime: CFG holds non-owning pointers to blocks.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include <unordered_map>
#include <vector>

namespace il::analysis
{

class CFG
{
  public:
    explicit CFG(const il::core::Function &f);

    const std::vector<const il::core::BasicBlock *> &predecessors(
        const il::core::BasicBlock *bb) const;
    const std::vector<const il::core::BasicBlock *> &successors(
        const il::core::BasicBlock *bb) const;

    const std::vector<const il::core::BasicBlock *> &postorder() const
    {
        return postOrder;
    }

    std::size_t postIndex(const il::core::BasicBlock *bb) const;

  private:
    using Block = il::core::BasicBlock;
    const il::core::Function &func;
    std::unordered_map<const Block *, std::vector<const Block *>> preds;
    std::unordered_map<const Block *, std::vector<const Block *>> succs;
    std::vector<const Block *> postOrder;
    std::unordered_map<const Block *, std::size_t> postNum;
};

} // namespace il::analysis
