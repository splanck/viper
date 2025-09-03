// File: src/analysis/CFG.hpp
// Purpose: Basic control-flow graph utilities for IL functions.
// Key invariants: Graph reflects explicit terminators; postorder is deterministic.
// Ownership/Lifetime: Views into existing function; does not own blocks.
// Links: docs/dev/analysis.md
#pragma once

#include "il/core/Function.hpp"
#include <unordered_map>
#include <vector>

namespace il::analysis
{

/// @brief Control-flow graph for a function.
/// Provides predecessor/successor queries and postorder numbering.
class CFG
{
  public:
    /// @brief Build CFG for function @p fn.
    explicit CFG(const il::core::Function &fn);

    /// @brief Successors of block @p bb.
    const std::vector<const il::core::BasicBlock *> &successors(
        const il::core::BasicBlock &bb) const;

    /// @brief Predecessors of block @p bb.
    const std::vector<const il::core::BasicBlock *> &predecessors(
        const il::core::BasicBlock &bb) const;

    /// @brief Postorder index of @p bb (0-based, root has highest index).
    size_t postorderIndex(const il::core::BasicBlock &bb) const;

    /// @brief Blocks in postorder (leaves first, entry last).
    const std::vector<const il::core::BasicBlock *> &postorderBlocks() const
    {
        return postorder;
    }

  private:
    std::unordered_map<const il::core::BasicBlock *, std::vector<const il::core::BasicBlock *>>
        succ;
    std::unordered_map<const il::core::BasicBlock *, std::vector<const il::core::BasicBlock *>>
        pred;
    std::vector<const il::core::BasicBlock *> postorder;
    std::unordered_map<const il::core::BasicBlock *, size_t> postIndex;

    void compute(const il::core::Function &fn);
};

} // namespace il::analysis
