// File: src/il/analysis/CFG.hpp
// Purpose: Build basic control-flow graph utilities for functions.
// Key invariants: Blocks are reachable and uniquely labeled.
// Ownership/Lifetime: Does not own the function or blocks.
// Links: docs/class-catalog.md
#pragma once

#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace il::analysis
{

/// @brief Computes CFG information for a function.
class CFG
{
  public:
    /// @brief Construct CFG for function @p f.
    explicit CFG(const core::Function &f);

    /// @brief Successor blocks of @p b.
    const std::vector<const core::BasicBlock *> &succs(const core::BasicBlock &b) const;

    /// @brief Predecessor blocks of @p b.
    const std::vector<const core::BasicBlock *> &preds(const core::BasicBlock &b) const;

    /// @brief Post-order index of @p b (0-based).
    unsigned postOrder(const core::BasicBlock &b) const;

    /// @brief Blocks in reverse post-order.
    const std::vector<const core::BasicBlock *> &rpo() const
    {
        return rpo_;
    }

  private:
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>> succs_;
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>> preds_;
    std::vector<const core::BasicBlock *> postOrder_;
    std::unordered_map<const core::BasicBlock *, unsigned> postIndex_;
    std::vector<const core::BasicBlock *> rpo_;

    void dfs(const core::BasicBlock *b, std::unordered_set<const core::BasicBlock *> &visited);
};

} // namespace il::analysis
