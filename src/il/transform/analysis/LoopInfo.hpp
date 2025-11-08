// File: src/il/transform/analysis/LoopInfo.hpp
// Purpose: Describe loop structure summaries derived from CFG and dominators.
// Key invariants: Loop membership is computed per function and stored by label for stability.
// Ownership/Lifetime: Returned summaries borrow labels and operate on caller-owned IL objects.
// Links: docs/codemap.md
#pragma once

#include "il/core/fwd.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace il::transform
{

/// \brief Summary of a single natural loop discovered in a function.
struct Loop
{
    /// Label identifying the loop header.
    std::string headerLabel;
    /// Labels of blocks that participate in the loop, including the header.
    std::vector<std::string> blockLabels;
    /// Labels of latch blocks (predecessors that branch back to the header).
    std::vector<std::string> latchLabels;

    /// \brief Determine whether @p label belongs to the loop body.
    [[nodiscard]] bool contains(std::string_view label) const;

  private:
    std::unordered_set<std::string> members_;

    friend class LoopInfo;
    void finalize();
};

/// \brief Loop collection discovered for a function.
class LoopInfo
{
  public:
    /// \brief Access the detected loops.
    [[nodiscard]] const std::vector<Loop> &loops() const
    {
        return loops_;
    }

    /// \brief Find the loop whose header has label @p headerLabel.
    [[nodiscard]] const Loop *findLoop(std::string_view headerLabel) const;

    /// \brief Add a loop description owned by the summary.
    void addLoop(Loop loop);

  private:
    std::vector<Loop> loops_;
};

/// \brief Compute loop information for @p function.
LoopInfo computeLoopInfo(il::core::Module &module, il::core::Function &function);

} // namespace il::transform
