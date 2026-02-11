//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: il/transform/analysis/Liveness.hpp
// Purpose: Liveness analysis for IL functions -- computes live-in and live-out
//          sets for SSA temporaries at each basic block using backward dataflow
//          fixpoint iteration over dense bitsets indexed by temporary ID.
// Key invariants:
//   - Liveness sets are conservative: a live temporary is guaranteed to be
//     used along some path from the program point.
//   - Bitset size equals the total number of SSA value IDs in the function.
// Ownership/Lifetime: LivenessInfo owns its bitset storage by value. CFGInfo
//          owns its adjacency maps. Both are returned as self-contained values
//          from their respective compute functions.
// Links: il/core/fwd.hpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include "il/core/fwd.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace il::transform
{

/// @brief Cached control-flow information for a function.
struct CFGInfo
{
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>> successors;
    std::unordered_map<const core::BasicBlock *, std::vector<const core::BasicBlock *>>
        predecessors;
};

/// @brief Cached liveness sets (live-in/live-out) for each block.
class LivenessInfo
{
  public:
    /// @brief Lightweight view over the live value bitset for a block edge.
    class SetView
    {
      public:
        SetView() = default;

        /// @brief Query whether the set contains @p valueId.
        /// @param valueId SSA temporary identifier.
        /// @return True when live; false otherwise.
        bool contains(unsigned valueId) const;

        template <typename Fn> void forEach(Fn &&fn) const
        {
            if (!bits_)
                return;
            for (unsigned id = 0; id < bits_->size(); ++id)
            {
                if ((*bits_)[id])
                    fn(id);
            }
        }

        /// @brief Check whether the set is empty.
        bool empty() const;

        /// @brief Access the underlying bitset representation.
        /// @return Reference to a vector<bool> view; may reference an empty sentinel.
        const std::vector<bool> &bits() const;

      private:
        explicit SetView(const std::vector<bool> *bits);

        const std::vector<bool> *bits_ = nullptr;

        friend class LivenessInfo;
    };

    /// @brief Live-in set for @p block.
    SetView liveIn(const core::BasicBlock &block) const;
    /// @overload
    SetView liveIn(const core::BasicBlock *block) const;

    /// @brief Live-out set for @p block.
    SetView liveOut(const core::BasicBlock &block) const;
    /// @overload
    SetView liveOut(const core::BasicBlock *block) const;

    /// @brief Total number of SSA value IDs tracked by the analysis.
    std::size_t valueCount() const;

  private:
    using BitSet = std::vector<bool>;

    std::size_t valueCount_{0};
    std::vector<const core::BasicBlock *> blocks_;
    std::unordered_map<const core::BasicBlock *, std::size_t> blockIndex_;
    std::vector<BitSet> liveInBits_;
    std::vector<BitSet> liveOutBits_;

    friend LivenessInfo computeLiveness(core::Module &module, core::Function &fn);
    friend LivenessInfo computeLiveness(core::Module &module,
                                        core::Function &fn,
                                        const CFGInfo &cfg);
};

/// @brief Build CFG adjacency information for a function.
CFGInfo buildCFG(core::Module &module, core::Function &fn);

/// @brief Compute liveness sets for @p fn by building a CFG internally.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn);

/// @brief Compute liveness sets for @p fn using a precomputed CFG.
LivenessInfo computeLiveness(core::Module &module, core::Function &fn, const CFGInfo &cfg);

} // namespace il::transform
