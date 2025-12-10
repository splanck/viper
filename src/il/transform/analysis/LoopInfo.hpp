//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares data structures and analysis routines for discovering and
// representing natural loops within IL functions. Loop information is fundamental
// to loop optimizations like LICM, loop unrolling, and induction variable analysis.
//
// Natural loop detection relies on the control flow graph and dominator tree.
// A natural loop is defined by a back edge (B → H where H dominates B): the loop
// consists of H (the header) and all blocks that can reach B without going through H.
// This file provides algorithms to identify such loops, compute loop membership,
// detect nesting relationships, and extract structural properties.
//
// Key Components:
// - Loop structure: Each Loop object represents a single natural loop, storing
//   the header block label, member block labels, latch blocks, and exit edges
// - Loop hierarchy: Nested loops form a tree structure where inner loops are
//   children of their immediately enclosing loop
// - Loop queries: Functions to test block membership, find loop preheaders,
//   identify exits, and compute loop depth
//
// The analysis stores loop information using block labels rather than pointers,
// maintaining stability across IL transformations that may reallocate blocks.
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

struct LoopExit
{
    std::string from;
    std::string to;
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
