//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/analysis/LoopInfo.hpp
// Purpose: Natural loop discovery and representation for IL functions. Each
//          Loop stores header label, member block labels, latch labels, exit
//          edges, and nesting relationships. LoopInfo collects all loops for
//          a function, supporting membership queries and parent lookups.
// Key invariants:
//   - Loop membership uses block labels (not pointers) for stability across
//     IR transformations that may reallocate blocks.
//   - A natural loop is defined by a back edge (B -> H where H dominates B).
// Ownership/Lifetime: LoopInfo and its Loop entries own their label strings
//          by value. Computed from Module + Function; result is self-contained.
// Links: il/core/fwd.hpp, il/analysis/Dominators.hpp, il/analysis/CFG.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/fwd.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace il::transform
{

/// @brief Describes an edge leaving a natural loop body.
/// @details An exit edge connects a block inside the loop (@c from) to a block
///          outside the loop (@c to). Exit edges are identified during loop
///          discovery by checking whether successor blocks belong to the loop body.
struct LoopExit
{
    std::string from; ///< Block label inside the loop that branches out.
    std::string to;   ///< Block label outside the loop that receives control.
};

/// @brief Hash functor for heterogeneous string lookup (C++20).
struct LoopStringHash
{
    using is_transparent = void;

    template <typename T> [[nodiscard]] std::size_t operator()(const T &key) const noexcept
    {
        return std::hash<std::string_view>{}(std::string_view(key));
    }
};

/// \brief Summary of a single natural loop discovered in a function.
struct Loop
{
    /// Label identifying the loop header.
    std::string headerLabel;
    /// Labels of blocks that participate in the loop, including the header.
    std::vector<std::string> blockLabels;
    /// Labels of latch blocks (predecessors that branch back to the header).
    std::vector<std::string> latchLabels;
    /// Labels of exit edges (from -> to) leaving the loop body.
    std::vector<LoopExit> exits;
    /// Child loop headers nested immediately inside this loop.
    std::vector<std::string> childHeaders;
    /// Parent loop header if nested, empty otherwise.
    std::string parentHeader;

    /// \brief Determine whether @p label belongs to the loop body.
    [[nodiscard]] bool contains(std::string_view label) const;

  private:
    std::unordered_set<std::string, LoopStringHash, std::equal_to<>> members_;

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

    /// \brief Find the parent loop for @p loop header.
    [[nodiscard]] const Loop *parent(const Loop &loop) const;

  private:
    friend LoopInfo computeLoopInfo(il::core::Module &module, il::core::Function &function);
    std::vector<Loop> loops_;
};

/// \brief Compute loop information for @p function.
LoopInfo computeLoopInfo(il::core::Module &module, il::core::Function &function);

} // namespace il::transform
